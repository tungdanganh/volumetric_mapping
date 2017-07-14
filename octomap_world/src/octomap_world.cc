/*
Copyright (c) 2015, Helen Oleynikova, ETH Zurich, Switzerland
You can contact the author at <helen dot oleynikova at mavt dot ethz dot ch>

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
* Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.
* Neither the name of ETHZ-ASL nor the
names of its contributors may be used to endorse or promote products
derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL ETHZ-ASL BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <ctime>

#include "octomap_world/octomap_world.h"

#include <glog/logging.h>
#include <octomap_msgs/conversions.h>
#include <octomap_ros/conversions.h>
#include <pcl/conversions.h>
#include <pcl/filters/filter.h>
#include <pcl_ros/transforms.h>



namespace volumetric_mapping {

// Convenience functions for octomap point <-> eigen conversions.
octomap::point3d pointEigenToOctomap(const Eigen::Vector3d& point) {
  return octomap::point3d(point.x(), point.y(), point.z());
}
Eigen::Vector3d pointOctomapToEigen(const octomap::point3d& point) {
  return Eigen::Vector3d(point.x(), point.y(), point.z());
}

// Create a default parameters object and call the other constructor with it.
OctomapWorld::OctomapWorld() : OctomapWorld(OctomapParameters()) {
}

// Creates an octomap with the correct parameters.
OctomapWorld::OctomapWorld(const OctomapParameters& params)
    : robot_size_(Eigen::Vector3d::Ones()) {
  setOctomapParameters(params);
}

void OctomapWorld::resetMap() {
  if (!octree_) {
    octree_.reset(new octomap::ColorOcTree(params_.resolution));
  }
  octree_->clear();
}

void OctomapWorld::prune() { octree_->prune(); }

void OctomapWorld::setOctomapParameters(const OctomapParameters& params) {
  if (octree_) {
    if (octree_->getResolution() != params.resolution) {
      LOG(WARNING) << "Octomap resolution has changed! Resetting tree!";
      octree_.reset(new octomap::ColorOcTree(params.resolution));
    }
  } else {
    octree_.reset(new octomap::ColorOcTree(params.resolution));
  }

  octree_->setProbHit(params.probability_hit);
  octree_->setProbMiss(params.probability_miss);
  octree_->setClampingThresMin(params.threshold_min);
  octree_->setClampingThresMax(params.threshold_max);
  octree_->setOccupancyThres(params.threshold_occupancy);
  octree_->enableChangeDetection(params.change_detection_enabled);

  // Copy over all the parameters for future use (some are not used just for
  // creating the octree).
  params_ = params;
}


void OctomapWorld::setCameraModelImpl(image_geometry::PinholeCameraModel& camInfo)
{
  ROS_WARN("got camera model..");
  CamModel = camInfo;
  std::cout << CamModel.cameraInfo() << std::endl;
}

void OctomapWorld::getVoxelParams(octomap::ColorOcTreeNode* node,
                                  VoxelParameters* param){
   //(this computer is little-endian)
  octomap::ColorOcTreeNode::Color color = node->getColor();
  float data = node->getValue();
  unsigned char* byte = (unsigned char*)(&data);
  param->timestamp = (int)byte[0] << 8 | (int)byte[1];
  param->counter = byte[2];
  param->saliency_temp = byte[3];
  param->saliency = color.r;

  switch (color.g) {
    case 0:
      param->type = VOXEL_NORMAL;
      break;
    case 1:
      param->type = VOXEL_SALIENCY;
      break;
    case 2:
      param->type = VOXEL_RETIRED;
      break;
    default:
      param->type = VOXEL_NORMAL;
      param->timestamp = salconfig_.timestamp;
      param->counter = 0;
      param->saliency_temp = 0;
      param->saliency = 0;
      break;
  }
}

void OctomapWorld::getVoxelParams(octomap::ColorOcTree::iterator node,
                                  VoxelParameters* param){
  octomap::ColorOcTreeNode::Color color = node->getColor();
  float data = node->getValue();
  unsigned char* byte = (unsigned char*)(&data);
  param->timestamp = ((int)byte[0] << 8) | (int)byte[1];
  param->counter = byte[2];
  param->saliency_temp = byte[3];
  param->saliency = color.r;

  switch (color.g) {
    case 0:
      param->type = VOXEL_NORMAL;
      break;
    case 1:
      param->type = VOXEL_SALIENCY;
      break;
    case 2:
      param->type = VOXEL_RETIRED;
      break;
    default:
      param->type = VOXEL_NORMAL;
      param->timestamp = salconfig_.timestamp;
      param->counter = 0;
      param->saliency_temp = 0;
      param->saliency = 0;
      break;
  }
}


void OctomapWorld::setVoxelParams(octomap::ColorOcTreeNode* node,
                                  VoxelParameters* param){
  float data;
  unsigned char* byte = (unsigned char *) (&data);
  *byte++ = (unsigned char)((param->timestamp >> 8) & 0x0000FF);
  *byte++ = (unsigned char)(param->timestamp & 0x0000FF);
  *byte++ = (unsigned char)(param->counter);
  *byte = (unsigned char)(param->saliency_temp);

  octomap::ColorOcTreeNode::Color color(param->saliency, (unsigned char)param->type, 0);
  node->setColor(color);
  node->setValue(data);
}

void OctomapWorld::setVoxelParams(octomap::ColorOcTree::iterator node,
                                  VoxelParameters* param){
  float data;
  unsigned char* byte = (unsigned char *) (&data);
  *byte++ = (unsigned char)((param->timestamp >> 8) & 0x0000FF);
  *byte++ = (unsigned char)(param->timestamp & 0x0000FF);
  *byte++ = (unsigned char)(param->counter);
  *byte = (unsigned char)(param->saliency_temp);

  octomap::ColorOcTreeNode::Color color(param->saliency, (unsigned char)param->type, 0);
  node->setColor(color);
  node->setValue(data);
}


void OctomapWorld::updateSaliency(octomap::ColorOcTreeNode * n, unsigned char sal_val)
{
  // TODO tung should change to params loading
  VoxelParameters voxel;
  getVoxelParams(n, &voxel);

  if (voxel.type == VOXEL_NORMAL)
  {
    float I_bar, I_bar_1;
    if(voxel.timestamp != salconfig_.timestamp) {
      // first hit in this time step
      voxel.counter = 0;
      voxel.timestamp = salconfig_.timestamp;
      voxel.saliency_temp = voxel.saliency;
    }
    I_bar_1 = voxel.saliency_temp;
    if (voxel.counter == 255) voxel.counter = 254; // temporary solution
    voxel.counter++;
    I_bar = (I_bar_1 * (voxel.counter-1) + sal_val)/voxel.counter;
    voxel.saliency += salconfig_.alpha * (I_bar - I_bar_1);
    voxel.saliency_temp = I_bar;

    voxel.type = (voxel.saliency > salconfig_.saliency_threshold)?VOXEL_SALIENCY:VOXEL_NORMAL;
    setVoxelParams(n, &voxel);

    //Check
    VoxelParameters voxel_check;
    getVoxelParams(n, &voxel_check);
    if ((voxel_check.type != voxel.type) ||
        (voxel_check.saliency != voxel.saliency) ||
        (voxel_check.timestamp != voxel.timestamp) ||
        (voxel_check.counter != voxel.counter) ||
        (voxel_check.saliency_temp != voxel.saliency_temp))
    {
      ROS_WARN("OMG_node: voxel_get %f %f %f %f %f", voxel_check.type, voxel_check.saliency, voxel_check.timestamp, voxel_check.counter, voxel_check.saliency_temp);
      ROS_WARN("OMG_node: voxel_set %f %f %f %f %f", voxel.type, voxel.saliency, voxel.timestamp, voxel.counter, voxel.saliency_temp);
    }
  }
  //std::cout << "Check: " << voxel_timestamp << " " << (int)voxel_counter << " " << voxel_type << " " << (int)voxel_saliency << std::endl;
}

void OctomapWorld::updateIOR(void)
{
  double exp_beta = 1 + salconfig_.beta + salconfig_.beta * salconfig_.beta / 2.0; // approximate of exponential function
  for (octomap::ColorOcTree::leaf_iterator it = octree_->begin_leafs(),
                          end = octree_->end_leafs(); it != end; ++it) {
    if (octree_->isNodeOccupied(*it)) {
      VoxelParameters voxel;
      getVoxelParams(it, &voxel);
      if ((voxel.type == VOXEL_SALIENCY) && (voxel.timestamp != salconfig_.timestamp)){

        double sal_tmp = (double)(voxel.saliency);
        sal_tmp *= exp_beta;
        voxel.saliency = (unsigned char)sal_tmp;
        voxel.type = (voxel.saliency > salconfig_.saliency_threshold)?VOXEL_SALIENCY:VOXEL_RETIRED;
        voxel.timestamp = salconfig_.timestamp;
        setVoxelParams(it, &voxel);

        // check
        VoxelParameters voxel_check;
        getVoxelParams(it, &voxel_check);
        if ((voxel_check.type != voxel.type) ||
            (voxel_check.saliency != voxel.saliency) ||
            (voxel_check.timestamp != voxel.timestamp) ||
            (voxel_check.counter != voxel.counter) ||
            (voxel_check.saliency_temp != voxel.saliency_temp))
        {
          ROS_WARN("OMG_it: voxel_get %f %f %f %f %f",
                    (float)voxel_check.type,
                    (float)voxel_check.saliency,
                    (float)voxel_check.timestamp,
                    (float)voxel_check.counter,
                    (float)voxel_check.saliency_temp);
          ROS_WARN("OMG_it: voxel_set %f %f %f %f %f",
                    (float)voxel.type,
                    (float)voxel.saliency,
                    (float)voxel.timestamp,
                    (float)voxel.counter,
                    (float)voxel.saliency_temp);
        }
      }
    }
  }
}

void OctomapWorld::insertSaliencyImageIntoMapImpl(
    const Transformation& T_G_sensor,
    const cv_bridge::CvImagePtr& img)
{
  salconfig_.timestamp++;
  clock_t begin_time = clock();

  const octomap::point3d origin = pointEigenToOctomap(T_G_sensor.getPosition());
  ProjCloud.clear();
  pcl::PointXYZ ptmp;
  ptmp.x = origin.x();
  ptmp.y = origin.y();
  ptmp.z = origin.z();
  ProjCloud.push_back(ptmp);

  // TUNG PROJECTION
  octomap::point3d direction = octomap::point3d (0,0.0,0.0);
  octomap::point3d obstacle(0,0,0);

  cv::Mat &mat = img->image;
  int width = mat.cols;
  int height = mat.rows;
  int count_success=0;
  int count_total=0;
  for (int i=0; i < width; i+=10)
  {
    for (int j=0; j < height; j+=10)
    {
      count_total++;
      cv::Point3d p;
      cv::Point2d uv_rect;
      uv_rect.x = i;
      uv_rect.y = j;
      p = CamModel.projectPixelTo3dRay(	uv_rect ) ;

      // transform to world coordinate
      // First, rotate the pointcloud into the world frame.
      pcl::PointCloud<pcl::PointXYZ>::Ptr cloud(new pcl::PointCloud<pcl::PointXYZ>());
      pcl::PointXYZ pnt, pntTf;
      pnt.x = p.x;
      pnt.y = p.y;
      pnt.z = p.z;
      cloud->clear();
      cloud->push_back(pnt);
      pcl::transformPointCloud(*cloud, *cloud, T_G_sensor.getTransformationMatrix());
      pntTf.x = cloud->points[0].x;
      pntTf.y = cloud->points[0].y;
      pntTf.z = cloud->points[0].z;
      // ray cast
      direction.x() = pntTf.x - origin.x();
      direction.y() = pntTf.y - origin.y();
      direction.z() = pntTf.z - origin.z();
      if (octree_->castRay(origin, direction, obstacle, false, -1)) // unlimited range
      {
        octomap::ColorOcTreeNode* n = octree_->search(obstacle);
        if (octree_->isNodeOccupied(n))
        {
          updateSaliency(n, 200); // mat.at<unsigned char>(j,i));
          count_success++;

          pcl::PointXYZ p_tmp;
          p_tmp.x = obstacle.x();
          p_tmp.y = obstacle.y();
          p_tmp.z = obstacle.z();
          ProjCloud.push_back(p_tmp);

        }
      }
    }
  }

  clock_t end_time = clock();
  double elapsed_secs = double(end_time - begin_time) / CLOCKS_PER_SEC;
  std::cout << "[" << salconfig_.timestamp << "] Projected " << count_total << " points with " << count_success << " successful points in (s): " << elapsed_secs << std::endl;

  begin_time = clock();
  updateIOR();
  end_time = clock();
  elapsed_secs = double(end_time - begin_time) / CLOCKS_PER_SEC;
  std::cout <<  "[" << salconfig_.timestamp << "] IOR (s) " << elapsed_secs << std::endl;
}


void OctomapWorld::generateProjectionMarker( const std::string& tf_frame,
    visualization_msgs::Marker* line_list)
{
  static int id = 0;
  if (ProjCloud.size() == 0) return;

  line_list->header.frame_id = tf_frame;
  line_list->header.stamp = ros::Time::now();
  line_list->ns = "points_and_lines";
  line_list->action = visualization_msgs::Marker::ADD;
  line_list->pose.orientation.w = 1.0;
  line_list->id = 0;
  line_list->type = visualization_msgs::Marker::LINE_LIST;
  line_list->scale.x = 0.1;
  line_list->color.r = 0.0f;
  line_list->color.g = 1.0f;
  line_list->color.b = 0.0f;
  line_list->color.a = 1.0;

  geometry_msgs::Point p1, p2;
  p1.x = ProjCloud.points[0].x;
  p1.y = ProjCloud.points[0].y;
  p1.z = ProjCloud.points[0].z;
  line_list->points.push_back(p1);

  int count = ProjCloud.size();

  for (int i = 1; i < count; i++)
  {
    p2.x = ProjCloud.points[i].x;
    p2.y = ProjCloud.points[i].y;
    p2.z = ProjCloud.points[i].z;
    // The line list needs two points for each line
    line_list->points.push_back(p1);
    line_list->points.push_back(p2);
  }
  line_list->points.push_back(p1);
}


void OctomapWorld::insertPointcloudColorIntoMapImpl(
    const Transformation& T_G_sensor,
    const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& cloud) {
  // Remove NaN values, if any.
  std::vector<int> indices;
  pcl::removeNaNFromPointCloud(*cloud, *cloud, indices);

  // First, rotate the pointcloud into the world frame.
  pcl::transformPointCloud(*cloud, *cloud,
                           T_G_sensor.getTransformationMatrix());
  const octomap::point3d p_G_sensor =
      pointEigenToOctomap(T_G_sensor.getPosition());

  // Then add all the rays from this pointcloud.
  // We do this as a batch operation - so first get all the keys in a set, then
  // do the update in batch.
  octomap::KeySet free_cells, occupied_cells;
  for (pcl::PointCloud<pcl::PointXYZRGB>::const_iterator it = cloud->begin();
       it != cloud->end(); ++it) {
    const octomap::point3d p_G_point(it->x, it->y, it->z);
    // First, check if we've already checked this.
    octomap::OcTreeKey key = octree_->coordToKey(p_G_point);

    if (occupied_cells.find(key) == occupied_cells.end()) {
      // Check if this is within the allowed sensor range.
      int res = castRay(p_G_sensor, p_G_point, &free_cells, &occupied_cells);

      if (res){
        // oocupied
        const int rgb = *reinterpret_cast<const int*>(&(it->rgb));
        unsigned char r = (rgb >> 16) & 0x0000ff;
        unsigned char g = (rgb >> 8)  & 0x0000ff;
        unsigned char b = (rgb)       & 0x0000ff;
        // if it is not salient point before
        octomap::ColorOcTreeNode* n = octree_->search(key);
        if (n != 0) {
          if (!(n->isColorSet())) {
            n->setColor(r, g, b);
          }
        }
        //
        // octomap::ColorOcTreeNode* node = octree_->search(p_G_point);
        // if (!(node->isColorSet()))
        //   octree_->averageNodeColor (key, r, g, b); //setNodeColor
        //
      }
    }
  }

  // Apply the new free cells and occupied cells from
  updateOccupancy(&free_cells, &occupied_cells);
}

void OctomapWorld::insertPointcloudIntoMapImpl(
    const Transformation& T_G_sensor,
    const pcl::PointCloud<pcl::PointXYZ>::Ptr& cloud) {
  // Remove NaN values, if any.
  std::vector<int> indices;
  pcl::removeNaNFromPointCloud(*cloud, *cloud, indices);

  // First, rotate the pointcloud into the world frame.
  pcl::transformPointCloud(*cloud, *cloud,
                           T_G_sensor.getTransformationMatrix());
  const octomap::point3d p_G_sensor =
      pointEigenToOctomap(T_G_sensor.getPosition());

  // Then add all the rays from this pointcloud.
  // We do this as a batch operation - so first get all the keys in a set, then
  // do the update in batch.
  octomap::KeySet free_cells, occupied_cells;
  for (pcl::PointCloud<pcl::PointXYZ>::const_iterator it = cloud->begin();
       it != cloud->end(); ++it) {
    const octomap::point3d p_G_point(it->x, it->y, it->z);
    // First, check if we've already checked this.
    octomap::OcTreeKey key = octree_->coordToKey(p_G_point);

    if (occupied_cells.find(key) == occupied_cells.end()) {
      // Check if this is within the allowed sensor range.
      castRay(p_G_sensor, p_G_point, &free_cells, &occupied_cells);
    }
  }

  // Apply the new free cells and occupied cells from
  updateOccupancy(&free_cells, &occupied_cells);

}

void OctomapWorld::insertProjectedDisparityIntoMapImpl(
    const Transformation& sensor_to_world, const cv::Mat& projected_points) {
  // Get the sensor origin in the world frame.
  Eigen::Vector3d sensor_origin_eigen = Eigen::Vector3d::Zero();
  sensor_origin_eigen = sensor_to_world * sensor_origin_eigen;
  octomap::point3d sensor_origin = pointEigenToOctomap(sensor_origin_eigen);

  octomap::KeySet free_cells, occupied_cells;
  for (int v = 0; v < projected_points.rows; ++v) {
    const cv::Vec3f* row_pointer = projected_points.ptr<cv::Vec3f>(v);

    for (int u = 0; u < projected_points.cols; ++u) {
      // Check whether we're within the correct range for disparity.
      if (!isValidPoint(row_pointer[u]) || row_pointer[u][2] < 0) {
        continue;
      }
      Eigen::Vector3d point_eigen(row_pointer[u][0], row_pointer[u][1],
                                  row_pointer[u][2]);

      point_eigen = sensor_to_world * point_eigen;
      octomap::point3d point_octomap = pointEigenToOctomap(point_eigen);

      // First, check if we've already checked this.
      octomap::OcTreeKey key = octree_->coordToKey(point_octomap);

      if (occupied_cells.find(key) == occupied_cells.end()) {
        // Check if this is within the allowed sensor range.
        castRay(sensor_origin, point_octomap, &free_cells, &occupied_cells);
      }
    }
  }
  updateOccupancy(&free_cells, &occupied_cells);
}

int OctomapWorld::castRay(const octomap::point3d& sensor_origin,
                           const octomap::point3d& point,
                           octomap::KeySet* free_cells,
                           octomap::KeySet* occupied_cells) const {
  CHECK_NOTNULL(free_cells);
  CHECK_NOTNULL(occupied_cells);

  int res = 0;

  if (params_.sensor_max_range < 0.0 ||
      (point - sensor_origin).norm() <= params_.sensor_max_range) {
    // Cast a ray to compute all the free cells.
    octomap::KeyRay key_ray;
    if (octree_->computeRayKeys(sensor_origin, point, key_ray)) {
      if (params_.max_free_space == 0.0) {
        free_cells->insert(key_ray.begin(), key_ray.end());
      } else {
        for (const auto& key: key_ray) {
          octomap::point3d voxel_coordinate = octree_->keyToCoord(key);
          if ((voxel_coordinate - sensor_origin).norm() < params_.max_free_space ||
              voxel_coordinate.z() > (sensor_origin.z() - params_.min_height_free_space)) {
            free_cells->insert(key);
          }
        }
      }
    }
    // Mark endpoing as occupied.
    octomap::OcTreeKey key;
    if (octree_->coordToKeyChecked(point, key)) {
      occupied_cells->insert(key);
      res = 1;
    }
  } else {
    // If the ray is longer than the max range, just update free space.
    octomap::point3d new_end =
        sensor_origin +
        (point - sensor_origin).normalized() * params_.sensor_max_range;
    octomap::KeyRay key_ray;
    if (octree_->computeRayKeys(sensor_origin, new_end, key_ray)) {
      if (params_.max_free_space == 0.0) {
        free_cells->insert(key_ray.begin(), key_ray.end());
      } else {
        for (const auto& key: key_ray) {
          octomap::point3d voxel_coordinate = octree_->keyToCoord(key);
          if ((voxel_coordinate - sensor_origin).norm() < params_.max_free_space ||
              voxel_coordinate.z() > (sensor_origin.z() - params_.min_height_free_space)) {
            free_cells->insert(key);
          }
        }
      }
    }
  }

  return res;
}


bool OctomapWorld::isValidPoint(const cv::Vec3f& point) const {
  // Check both for disparities explicitly marked as invalid (where OpenCV maps
  // pt.z to MISSING_Z) and zero disparities (point mapped to infinity).
  return point[2] != 10000.0f && !std::isinf(point[2]);
}

void OctomapWorld::updateOccupancy(octomap::KeySet* free_cells,
                                   octomap::KeySet* occupied_cells) {
  CHECK_NOTNULL(free_cells);
  CHECK_NOTNULL(occupied_cells);

  // Mark occupied cells.
  for (octomap::KeySet::iterator it = occupied_cells->begin(),
                                 end = occupied_cells->end();
       it != end; it++) {
    octree_->updateNode(*it, true);

    // Remove any occupied cells from free cells - assume there are far fewer
    // occupied cells than free cells, so this is much faster than checking on
    // every free cell.
    if (free_cells->find(*it) != free_cells->end()) {
      free_cells->erase(*it);
    }
  }

  // Mark free cells.
  for (octomap::KeySet::iterator it = free_cells->begin(),
                                 end = free_cells->end();
       it != end; ++it) {
    octree_->updateNode(*it, false);
  }
  octree_->updateInnerOccupancy();
}

OctomapWorld::CellStatus OctomapWorld::getCellStatusBoundingBox(
    const Eigen::Vector3d& point,
    const Eigen::Vector3d& bounding_box_size) const {
  // First case: center point is unknown or occupied. Can just quit.
  CellStatus center_status = getCellStatusPoint(point);
  if (center_status != CellStatus::kFree && params_.treat_unknown_as_occupied) {
    return center_status;
  }

  // Also if center is outside of the bounds.
  octomap::OcTreeKey key;
  if (!octree_->coordToKeyChecked(pointEigenToOctomap(point), key)) {
    if (params_.treat_unknown_as_occupied) {
      return CellStatus::kUnknown;
    } else {
      return CellStatus::kOccupied;
    }
  }

  // Now we have to iterate over everything in the bounding box.
  Eigen::Vector3d bbx_min_eigen = point - bounding_box_size / 2;
  Eigen::Vector3d bbx_max_eigen = point + bounding_box_size / 2;

  octomap::point3d bbx_min = pointEigenToOctomap(bbx_min_eigen);
  octomap::point3d bbx_max = pointEigenToOctomap(bbx_max_eigen);

  for (octomap::ColorOcTree::leaf_bbx_iterator
           iter = octree_->begin_leafs_bbx(bbx_min, bbx_max),
           end = octree_->end_leafs_bbx();
       iter != end; ++iter) {
    if (octree_->isNodeOccupied(*iter)) {
      if (params_.filter_speckles && isSpeckleNode(iter.getKey())) {
        continue;
      } else {
        return CellStatus::kOccupied;
      }
    }
  }

  // The above only returns valid nodes - we should check for unknown nodes as
  // well.
  octomap::point3d_list unknown_centers;
  octree_->getUnknownLeafCenters(unknown_centers, bbx_min, bbx_max);
  if (unknown_centers.size() > 0) {
    return CellStatus::kUnknown;
  }
  return CellStatus::kFree;
}

OctomapWorld::CellStatus OctomapWorld::getCellStatusPoint(
    const Eigen::Vector3d& point) const {
  octomap::OcTreeNode* node = octree_->search(point.x(), point.y(), point.z());
  if (node == NULL) {
    return CellStatus::kUnknown;
  } else if (octree_->isNodeOccupied(node)) {
    return CellStatus::kOccupied;
  } else {
    return CellStatus::kFree;
  }
}

OctomapWorld::CellStatus OctomapWorld::getCellProbabilityPoint(
    const Eigen::Vector3d& point, double* probability) const {
  octomap::OcTreeNode* node = octree_->search(point.x(), point.y(), point.z());
  if (node == NULL) {
    if (probability) {
      *probability = -1.0;
    }
    return CellStatus::kUnknown;
  } else {
    if (probability) {
      *probability = node->getOccupancy();
    }
    if (octree_->isNodeOccupied(node)) {
      return CellStatus::kOccupied;
    } else {
      return CellStatus::kFree;
    }
  }
}

OctomapWorld::CellStatus OctomapWorld::getCuriousGain(
    const Eigen::Vector3d& point, double* gain) const {
  octomap::ColorOcTreeNode* node = octree_->search(point.x(), point.y(), point.z());
  *gain = 0;
  if (node == NULL) {
    return CellStatus::kUnknown;
  } else {
    if (octree_->isNodeOccupied(node)) {
      octomap::ColorOcTreeNode::Color color = node->getColor();
      *gain = color.b;
      return CellStatus::kOccupied;
    } else {
      return CellStatus::kFree;
    }
  }
}

OctomapWorld::CellStatus OctomapWorld::getLineStatus(
    const Eigen::Vector3d& start, const Eigen::Vector3d& end) const {
  // Get all node keys for this line.
  // This is actually a typedef for a vector of OcTreeKeys.
  octomap::KeyRay key_ray;

  octree_->computeRayKeys(pointEigenToOctomap(start), pointEigenToOctomap(end),
                          key_ray);

  // Now check if there are any unknown or occupied nodes in the ray.
  for (octomap::OcTreeKey key : key_ray) {
    octomap::OcTreeNode* node = octree_->search(key);
    if (node == NULL) {
      return CellStatus::kUnknown;
    } else if (octree_->isNodeOccupied(node)) {
      return CellStatus::kOccupied;
    }
  }
  return CellStatus::kFree;
}

OctomapWorld::CellStatus OctomapWorld::getVisibility(
    const Eigen::Vector3d& view_point, const Eigen::Vector3d& voxel_to_test,
    bool stop_at_unknown_cell) const {
  // Get all node keys for this line.
  // This is actually a typedef for a vector of OcTreeKeys.
  octomap::KeyRay key_ray;

  octree_->computeRayKeys(pointEigenToOctomap(view_point),
                          pointEigenToOctomap(voxel_to_test), key_ray);

  const octomap::OcTreeKey& voxel_to_test_key =
      octree_->coordToKey(pointEigenToOctomap(voxel_to_test));

  // Now check if there are any unknown or occupied nodes in the ray,
  // except for the voxel_to_test key.
  for (octomap::OcTreeKey key : key_ray) {
    if (key != voxel_to_test_key) {
      octomap::OcTreeNode* node = octree_->search(key);
      if (node == NULL) {
        if (stop_at_unknown_cell) {
          return CellStatus::kUnknown;
        }
      } else if (octree_->isNodeOccupied(node)) {
        return CellStatus::kOccupied;
      }
    }
  }
  return CellStatus::kFree;
}

OctomapWorld::CellStatus OctomapWorld::getLineStatusBoundingBox(
    const Eigen::Vector3d& start, const Eigen::Vector3d& end,
    const Eigen::Vector3d& bounding_box_size) const {
  // TODO(helenol): Probably best way would be to get all the coordinates along
  // the line, then make a set of all the OcTreeKeys in all the bounding boxes
  // around the nodes... and then just go through and query once.
  const double epsilon = 0.001;  // Small offset
  CellStatus ret = CellStatus::kFree;
  const double& resolution = getResolution();

  // Check corner connections and depending on resolution also interior:
  // Discretization step is smaller than the octomap resolution, as this way
  // no cell can possibly be missed
  double x_disc = bounding_box_size.x() /
                  ceil((bounding_box_size.x() + epsilon) / resolution);
  double y_disc = bounding_box_size.y() /
                  ceil((bounding_box_size.y() + epsilon) / resolution);
  double z_disc = bounding_box_size.z() /
                  ceil((bounding_box_size.z() + epsilon) / resolution);

  // Ensure that resolution is not infinit
  if (x_disc <= 0.0) x_disc = 1.0;
  if (y_disc <= 0.0) y_disc = 1.0;
  if (z_disc <= 0.0) z_disc = 1.0;

  const Eigen::Vector3d bounding_box_half_size = bounding_box_size * 0.5;

  for (double x = -bounding_box_half_size.x(); x <= bounding_box_half_size.x();
       x += x_disc) {
    for (double y = -bounding_box_half_size.y();
         y <= bounding_box_half_size.y(); y += y_disc) {
      for (double z = -bounding_box_half_size.z();
           z <= bounding_box_half_size.z(); z += z_disc) {
        Eigen::Vector3d offset(x, y, z);
        ret = getLineStatus(start + offset, end + offset);
        if (ret != CellStatus::kFree) {
          return ret;
        }
      }
    }
  }
  return CellStatus::kFree;
}

double OctomapWorld::getResolution() const { return octree_->getResolution(); }

void OctomapWorld::setFree(const Eigen::Vector3d& position,
                           const Eigen::Vector3d& bounding_box_size) {
  setLogOddsBoundingBox(position, bounding_box_size,
                        octree_->getClampingThresMinLog());
}

void OctomapWorld::setOccupied(const Eigen::Vector3d& position,
                               const Eigen::Vector3d& bounding_box_size) {
  setLogOddsBoundingBox(position, bounding_box_size,
                        octree_->getClampingThresMaxLog());
}

void OctomapWorld::getOccupiedPointCloud(
    pcl::PointCloud<pcl::PointXYZ>* output_cloud) const {
  CHECK_NOTNULL(output_cloud)->clear();
  unsigned int max_tree_depth = octree_->getTreeDepth();
  double resolution = octree_->getResolution();
  for (octomap::ColorOcTree::leaf_iterator it = octree_->begin_leafs();
       it != octree_->end_leafs(); ++it) {
    if (octree_->isNodeOccupied(*it)) {
      // If leaf is max depth add coordinates.
      if (max_tree_depth == it.getDepth()) {
        pcl::PointXYZ point(it.getX(), it.getY(), it.getZ());
        output_cloud->push_back(point);
      }
      // If leaf is not max depth it represents an occupied voxel with edge
      // length of 2^(max_tree_depth - leaf_depth) * resolution.
      // We use multiple points to visualize this filled volume.
      else {
        const unsigned int box_edge_length =
            pow(2,max_tree_depth - it.getDepth() - 1);
        const double bbx_offset = box_edge_length * resolution - resolution/2;
        Eigen::Vector3d bbx_offset_vec(bbx_offset, bbx_offset, bbx_offset);
        Eigen::Vector3d center(it.getX(), it.getY(), it.getZ());
        Eigen::Vector3d bbx_min = center - bbx_offset_vec;
        Eigen::Vector3d bbx_max = center + bbx_offset_vec;
        // Add small offset to avoid overshooting bbx_max.
        bbx_max += Eigen::Vector3d(0.001, 0.001, 0.001);
        for (double x_position = bbx_min.x(); x_position <= bbx_max.x();
             x_position += resolution) {
          for (double y_position = bbx_min.y(); y_position <= bbx_max.y();
               y_position += resolution) {
            for (double z_position = bbx_min.z(); z_position <= bbx_max.z();
                 z_position += resolution) {
              output_cloud->push_back(pcl::PointXYZ(x_position,
                                                    y_position,
                                                    z_position));
            }
          }
        }
      }
    }
  }
}

void OctomapWorld::getOccupiedPointcloudInBoundingBox(
    const Eigen::Vector3d& center, const Eigen::Vector3d& bounding_box_size,
    pcl::PointCloud<pcl::PointXYZ>* output_cloud) const {
  CHECK_NOTNULL(output_cloud);
  output_cloud->clear();

  const double resolution = octree_->getResolution();
  const double epsilon = 0.001;  // Small offset to not hit boundary of nodes.
  Eigen::Vector3d epsilon_3d;
  epsilon_3d.setConstant(epsilon);

  // Determine correct center of voxel.
  const Eigen::Vector3d center_corrected(
      resolution * std::floor(center.x() / resolution) + resolution / 2.0,
      resolution * std::floor(center.y() / resolution) + resolution / 2.0,
      resolution * std::floor(center.z() / resolution) + resolution / 2.0);

  Eigen::Vector3d bbx_min =
      center_corrected - bounding_box_size / 2 - epsilon_3d;
  Eigen::Vector3d bbx_max =
      center_corrected + bounding_box_size / 2 + epsilon_3d;

  for (double x_position = bbx_min.x(); x_position <= bbx_max.x();
       x_position += resolution) {
    for (double y_position = bbx_min.y(); y_position <= bbx_max.y();
         y_position += resolution) {
      for (double z_position = bbx_min.z(); z_position <= bbx_max.z();
           z_position += resolution) {
        octomap::point3d point =
            octomap::point3d(x_position, y_position, z_position);
        octomap::OcTreeKey key = octree_->coordToKey(point);
        octomap::OcTreeNode* node = octree_->search(key);
        if (node != NULL && octree_->isNodeOccupied(node)) {
          output_cloud->push_back(
              pcl::PointXYZ(point.x(), point.y(), point.z()));
        }
      }
    }
  }
}

void OctomapWorld::getAllFreeBoxes(
    std::vector<std::pair<Eigen::Vector3d, double> >* free_box_vector) const {
  const bool occupied_boxes = false;
  getAllBoxes(occupied_boxes, free_box_vector);
}

void OctomapWorld::getAllOccupiedBoxes(
    std::vector<std::pair<Eigen::Vector3d, double> >* occupied_box_vector)
    const {
  const bool occupied_boxes = true;
  getAllBoxes(occupied_boxes, occupied_box_vector);
}

void OctomapWorld::getAllBoxes(
    bool occupied_boxes,
    std::vector<std::pair<Eigen::Vector3d, double> >* box_vector) const {
  box_vector->clear();
  box_vector->reserve(octree_->size());
  for (octomap::ColorOcTree::leaf_iterator it = octree_->begin_leafs(),
                                      end = octree_->end_leafs();
       it != end; ++it) {
    Eigen::Vector3d cube_center(it.getX(), it.getY(), it.getZ());
    int depth_level = it.getDepth();
    double cube_size = octree_->getNodeSize(depth_level);

    if (octree_->isNodeOccupied(*it) && occupied_boxes) {
      box_vector->emplace_back(cube_center, cube_size);
    } else if (!octree_->isNodeOccupied(*it) && !occupied_boxes) {
      box_vector->emplace_back(cube_center, cube_size);
    }
  }
}

void OctomapWorld::setLogOddsBoundingBox(
    const Eigen::Vector3d& position, const Eigen::Vector3d& bounding_box_size,
    double log_odds_value) {
  const bool lazy_eval = true;
  const double resolution = octree_->getResolution();
  const double epsilon = 0.001;  // Small offset to not hit boundary of nodes.
  Eigen::Vector3d epsilon_3d;
  epsilon_3d.setConstant(epsilon);

  Eigen::Vector3d bbx_min = position - bounding_box_size / 2 - epsilon_3d;
  Eigen::Vector3d bbx_max = position + bounding_box_size / 2 + epsilon_3d;

  for (double x_position = bbx_min.x(); x_position <= bbx_max.x();
       x_position += resolution) {
    for (double y_position = bbx_min.y(); y_position <= bbx_max.y();
         y_position += resolution) {
      for (double z_position = bbx_min.z(); z_position <= bbx_max.z();
           z_position += resolution) {
        octomap::point3d point =
            octomap::point3d(x_position, y_position, z_position);
        octree_->setNodeValue(point, log_odds_value, lazy_eval);
      }
    }
  }
  // This is necessary since lazy_eval is set to true.
  octree_->updateInnerOccupancy();
}

bool OctomapWorld::getOctomapBinaryMsg(octomap_msgs::Octomap* msg) const {
  return octomap_msgs::binaryMapToMsg(*octree_, *msg);
}

bool OctomapWorld::getOctomapFullMsg(octomap_msgs::Octomap* msg) const {
  return octomap_msgs::fullMapToMsg(*octree_, *msg);
}

void OctomapWorld::setOctomapFromMsg(const octomap_msgs::Octomap& msg) {
  if (msg.binary) {
    setOctomapFromBinaryMsg(msg);
  } else {
    setOctomapFromFullMsg(msg);
  }
}

void OctomapWorld::setOctomapFromBinaryMsg(const octomap_msgs::Octomap& msg) {
  octree_.reset(
      dynamic_cast<octomap::ColorOcTree*>(octomap_msgs::binaryMsgToMap(msg)));
}

void OctomapWorld::setOctomapFromFullMsg(const octomap_msgs::Octomap& msg) {
  octree_.reset(
      dynamic_cast<octomap::ColorOcTree*>(octomap_msgs::fullMsgToMap(msg)));
}

bool OctomapWorld::loadOctomapFromFile(const std::string& filename) {
  if (!octree_) {
    // TODO(helenol): Resolution shouldn't matter... I think. I'm not sure.
    octree_.reset(new octomap::ColorOcTree(0.05));
  }
  return octree_->readBinary(filename);
}

bool OctomapWorld::writeOctomapToFile(const std::string& filename) {
  return octree_->writeBinary(filename);
}

bool OctomapWorld::isSpeckleNode(const octomap::OcTreeKey& key) const {
  octomap::OcTreeKey current_key;
  // Search neighbors in a +/-1 key range cube. If there are neighbors, it's
  // not a speckle.
  bool neighbor_found = false;
  for (current_key[2] = key[2] - 1; current_key[2] <= key[2] + 1;
       ++current_key[2]) {
    for (current_key[1] = key[1] - 1; current_key[1] <= key[1] + 1;
         ++current_key[1]) {
      for (current_key[0] = key[0] - 1; current_key[0] <= key[0] + 1;
           ++current_key[0]) {
        if (current_key != key) {
          octomap::OcTreeNode* node = octree_->search(key);
          if (node && octree_->isNodeOccupied(node)) {
            // We have a neighbor => not a speckle!
            return false;
          }
        }
      }
    }
  }
  return true;
}

void OctomapWorld::generateMarkerArray(
    const std::string& tf_frame,
    visualization_msgs::MarkerArray* occupied_nodes,
    visualization_msgs::MarkerArray* free_nodes) {
  CHECK_NOTNULL(occupied_nodes);
  CHECK_NOTNULL(free_nodes);

  // Prune the octree first.
  // TODO Tung: disable prune
  octree_->prune();
  int tree_depth = octree_->getTreeDepth() + 1;

  // In the marker array, assign each node to its respective depth level, since
  // all markers in a CUBE_LIST must have the same scale.
  occupied_nodes->markers.resize(tree_depth);
  free_nodes->markers.resize(tree_depth);

  // Metric min and max z of the map:
  double min_x, min_y, min_z, max_x, max_y, max_z;
  octree_->getMetricMin(min_x, min_y, min_z);
  octree_->getMetricMax(max_x, max_y, max_z);

  // Update values from params if necessary.
  if (params_.visualize_min_z > min_z) {
    min_z = params_.visualize_min_z;
  }
  if (params_.visualize_max_z < max_z) {
    max_z = params_.visualize_max_z;
  }

  for (int i = 0; i < tree_depth; ++i) {
    double size = octree_->getNodeSize(i);

    occupied_nodes->markers[i].header.frame_id = tf_frame;
    occupied_nodes->markers[i].ns = "map";
    occupied_nodes->markers[i].id = i;
    occupied_nodes->markers[i].type = visualization_msgs::Marker::CUBE_LIST;
    occupied_nodes->markers[i].scale.x = size;
    occupied_nodes->markers[i].scale.y = size;
    occupied_nodes->markers[i].scale.z = size;

    free_nodes->markers[i] = occupied_nodes->markers[i];
  }

  for (octomap::ColorOcTree::leaf_iterator it = octree_->begin_leafs(),
                                      end = octree_->end_leafs();
       it != end; ++it) {
    geometry_msgs::Point cube_center;
    cube_center.x = it.getX();
    cube_center.y = it.getY();
    cube_center.z = it.getZ();

    if (cube_center.z > max_z || cube_center.z < min_z) {
      continue;
    }

    int depth_level = it.getDepth();

    std_msgs::ColorRGBA marker_color;
    octomap::ColorOcTreeNode::Color oc_color;

    if (octree_->isNodeOccupied(*it)) {
      occupied_nodes->markers[depth_level].points.push_back(cube_center);

#ifdef OCTOMAP_IS_COLORED
      occupied_nodes->markers[depth_level].colors.push_back(
             getEncodedColor(it));
#else
      occupied_nodes->markers[depth_level].colors.push_back(
          percentToColor(colorizeMapByHeight(it.getZ(), min_z, max_z)));
#endif

    } else {
      free_nodes->markers[depth_level].points.push_back(cube_center);
      free_nodes->markers[depth_level].colors.push_back(
          percentToColor(colorizeMapByHeight(it.getZ(), min_z, max_z)));
    }
  }

  for (int i = 0; i < tree_depth; ++i) {
    if (occupied_nodes->markers[i].points.size() > 0) {
      occupied_nodes->markers[i].action = visualization_msgs::Marker::ADD;
    } else {
      occupied_nodes->markers[i].action = visualization_msgs::Marker::DELETE;
    }

    if (free_nodes->markers[i].points.size() > 0) {
      free_nodes->markers[i].action = visualization_msgs::Marker::ADD;
    } else {
      free_nodes->markers[i].action = visualization_msgs::Marker::DELETE;
    }
  }
}
std_msgs::ColorRGBA OctomapWorld::getEncodedColor(octomap::ColorOcTree::iterator it)
{

  VoxelParameters voxel;
  getVoxelParams(it, &voxel);
  std_msgs::ColorRGBA color;
  color.a = 1;

  if (voxel.type == VOXEL_SALIENCY)
  {
    color.r = voxel.saliency;
    color.g = 0;
    color.b = 0;
  }
  else if (voxel.type == VOXEL_RETIRED)
  {
    color.r = 0;
    color.g = 100;
    color.b = 0;
  }
  else
  {
    color.r = 0;
    color.g = 0;
    color.b = 100;
  }

  return color;
}

double OctomapWorld::colorizeMapByHeight(double z, double min_z,
                                         double max_z) const {
  return (1.0 - std::min(std::max((z - min_z) / (max_z - min_z), 0.0), 1.0));
}

std_msgs::ColorRGBA OctomapWorld::percentToColor(double h) const {
  // Helen's note: direct copy from OctomapProvider.
  std_msgs::ColorRGBA color;
  color.a = 1.0;
  // blend over HSV-values (more colors)

  double s = 1.0;
  double v = 1.0;

  h -= floor(h);
  h *= 6;
  int i;
  double m, n, f;

  i = floor(h);
  f = h - i;
  if (!(i & 1)) f = 1 - f;  // if i is even
  m = v * (1 - s);
  n = v * (1 - s * f);

  switch (i) {
    case 6:
    case 0:
      color.r = v;
      color.g = n;
      color.b = m;
      break;
    case 1:
      color.r = n;
      color.g = v;
      color.b = m;
      break;
    case 2:
      color.r = m;
      color.g = v;
      color.b = n;
      break;
    case 3:
      color.r = m;
      color.g = n;
      color.b = v;
      break;
    case 4:
      color.r = n;
      color.g = m;
      color.b = v;
      break;
    case 5:
      color.r = v;
      color.g = m;
      color.b = n;
      break;
    default:
      color.r = 1;
      color.g = 0.5;
      color.b = 0.5;
      break;
  }

  return color;
}

Eigen::Vector3d OctomapWorld::getMapCenter() const {
  // Metric min and max z of the map:
  double min_x, min_y, min_z, max_x, max_y, max_z;
  octree_->getMetricMin(min_x, min_y, min_z);
  octree_->getMetricMax(max_x, max_y, max_z);

  Eigen::Vector3d min_3d(min_x, min_y, min_z);
  Eigen::Vector3d max_3d(max_x, max_y, max_z);

  return min_3d + (max_3d - min_3d) / 2;
}

Eigen::Vector3d OctomapWorld::getMapSize() const {
  // Metric min and max z of the map:
  double min_x, min_y, min_z, max_x, max_y, max_z;
  octree_->getMetricMin(min_x, min_y, min_z);
  octree_->getMetricMax(max_x, max_y, max_z);

  return Eigen::Vector3d(max_x - min_x, max_y - min_y, max_z - min_z);
}

void OctomapWorld::getMapBounds(Eigen::Vector3d* min_bound,
                                Eigen::Vector3d* max_bound) const {
  CHECK_NOTNULL(min_bound);
  CHECK_NOTNULL(max_bound);
  // Metric min and max z of the map:
  double min_x, min_y, min_z, max_x, max_y, max_z;
  octree_->getMetricMin(min_x, min_y, min_z);
  octree_->getMetricMax(max_x, max_y, max_z);

  *min_bound = Eigen::Vector3d(min_x, min_y, min_z);
  *max_bound = Eigen::Vector3d(max_x, max_y, max_z);
}

void OctomapWorld::setRobotSize(const Eigen::Vector3d& robot_size) {
  robot_size_ = robot_size;
}

Eigen::Vector3d OctomapWorld::getRobotSize() const { return robot_size_; }

bool OctomapWorld::checkCollisionWithRobot(
    const Eigen::Vector3d& robot_position) {
  return checkSinglePoseCollision(robot_position);
}

bool OctomapWorld::checkPathForCollisionsWithRobot(
    const std::vector<Eigen::Vector3d>& robot_positions,
    size_t* collision_index) {
  // Iterate over vector of poses.
  // Check each one.
  // Return when a collision is found, and return the index of the earliest
  // collision.
  for (size_t i = 0; i < robot_positions.size(); ++i) {
    if (checkSinglePoseCollision(robot_positions[i])) {
      if (collision_index != nullptr) {
        *collision_index = i;
      }
      return true;
    }
  }
  return false;
}

bool OctomapWorld::checkSinglePoseCollision(
    const Eigen::Vector3d& robot_position) const {
  if (params_.treat_unknown_as_occupied) {
    return (CellStatus::kFree !=
            getCellStatusBoundingBox(robot_position, robot_size_));
  } else {
    return (CellStatus::kOccupied ==
            getCellStatusBoundingBox(robot_position, robot_size_));
  }
}

void OctomapWorld::getChangedPoints(
    std::vector<Eigen::Vector3d>* changed_points,
    std::vector<bool>* changed_states) {
  CHECK_NOTNULL(changed_points);
  // These keys are always *leaf node* keys, even if the actual change was in
  // a larger cube (see Octomap docs).
  octomap::KeyBoolMap::const_iterator start_key = octree_->changedKeysBegin();
  octomap::KeyBoolMap::const_iterator end_key = octree_->changedKeysEnd();

  changed_points->clear();
  if (changed_states != NULL) {
    changed_states->clear();
  }

  for (octomap::KeyBoolMap::const_iterator iter = start_key; iter != end_key;
       ++iter) {
    octomap::OcTreeNode* node = octree_->search(iter->first);
    bool occupied = octree_->isNodeOccupied(node);
    Eigen::Vector3d center =
        pointOctomapToEigen(octree_->keyToCoord(iter->first));

    changed_points->push_back(center);
    if (changed_states != NULL) {
      changed_states->push_back(occupied);
    }
  }
  octree_->resetChangeDetection();
}

}  // namespace volumetric_mapping
