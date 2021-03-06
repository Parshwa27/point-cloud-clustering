#pragma once

#include <dirent.h>
#include <iostream>
#include <string>
#include <sstream>
#include <unordered_set>
#include <pcl/ModelCoefficients.h>
#include <pcl/io/pcd_io.h>
#include <pcl/point_types.h>
#include <pcl/filters/crop_box.h>
#include <pcl/filters/impl/crop_box.hpp>
#include <Eigen/Dense>
#include <Eigen/Geometry>

#include "dataset_generation/json.hpp"

namespace dataset_generation
{

using json = nlohmann::json;

class ExtractPointCloudObjects
{

public:
    ExtractPointCloudObjects(
        std::string in_folder_pcd,
        std::string in_folder_dets3d,
        std::string out_folder_pcd);
    
    void extract_objects_from_all_pcds();
    
    bool extract_objects_from_pcd(
        pcl::PCLPointCloud2 in_cloud_blob,
        const json& dets3d_json,
        const std::string& pcd_fn);

private:
    std::string in_folder_pcd_;
    std::string in_folder_dets3d_;
    std::string out_folder_pcd_;

    std::unordered_set<std::string> pcd_names_set_, dets3d_names_set_;

    int common_fn_ = 0;
    int not_common_fn_ = 0;

    std::unordered_map<std::string, int> labels_count_map_;
};

void get_files_in_directory(
    const std::string& directory,
    std::unordered_set<std::string>& s)
{
    DIR *dir;
    class dirent *ent;
    class stat st;

    dir = opendir(directory.c_str());
    while ((ent = readdir(dir)) != NULL) {
        const std::string file_name = ent->d_name;
        const std::string full_file_name = directory + "/" + file_name;

        if (file_name[0] == '.')
            continue;

        if (stat(full_file_name.c_str(), &st) == -1)
            continue;

        const bool is_directory = (st.st_mode & S_IFDIR) != 0;

        if (is_directory)
            continue;

        s.insert(file_name);
    }
    closedir(dir);
}

ExtractPointCloudObjects::ExtractPointCloudObjects(
    std::string in_folder_pcd,
    std::string in_folder_dets3d,
    std::string out_folder_pcd):
    in_folder_pcd_(in_folder_pcd),
    in_folder_dets3d_(in_folder_dets3d),
    out_folder_pcd_(out_folder_pcd)
{   
    // Get list of all files in the directories
    get_files_in_directory(in_folder_pcd_, pcd_names_set_);
    get_files_in_directory(in_folder_dets3d_, dets3d_names_set_);

    // Ensure that all PCD files have their respective JSON files
    for (auto it = pcd_names_set_.begin(); it != pcd_names_set_.end(); it++)
    {
        std::string pcd_fn = *it;
        std::string dets3d_fn = pcd_fn.substr(0, pcd_fn.size() - 3) + "json";
        
        // If not found, display on screen and remove that PCD from set
        if (dets3d_names_set_.find(dets3d_fn) == dets3d_names_set_.end())
        {
            std::cout << "Detections for " << pcd_fn << " not found!" << std::endl;
            pcd_names_set_.erase(pcd_fn);
            not_common_fn_++;
        }
        else
        {
            common_fn_++;
        }
    }
    std::cout << "Number of common files: " << common_fn_ << std::endl;
}

void ExtractPointCloudObjects::extract_objects_from_all_pcds()
{
    // Iterate over all PCD files, find their corresponding detections
    for (auto it = pcd_names_set_.begin(); it != pcd_names_set_.end(); it++)
    {
        std::string pcd_fn = *it;
        std::string dets3d_fn = pcd_fn.substr(0, pcd_fn.size() - 3) + "json";

        std::string pcd_file_path, dets3d_file_path;
        pcd_file_path = in_folder_pcd_ + "/" + pcd_fn;
        dets3d_file_path = in_folder_dets3d_ + "/" + dets3d_fn;

        pcl::PCLPointCloud2 in_cloud_blob;
        pcl::io::loadPCDFile (pcd_file_path, in_cloud_blob);
        
        std::ifstream dets3d_file(dets3d_file_path);
        json dets3d_json;
        dets3d_file >> dets3d_json;

        bool exported = extract_objects_from_pcd(in_cloud_blob, dets3d_json, pcd_fn);
    }

    // Print out how many objects were found
    std::cout << "Number of objects found: " << std::endl;
    for (auto it = labels_count_map_.begin(); it != labels_count_map_.end(); it++)
    {
        std::cout << it->first << ": " << it->second << std::endl;
    }
}

bool ExtractPointCloudObjects::extract_objects_from_pcd(
    pcl::PCLPointCloud2 in_cloud_blob,
    const json& dets3d_json, 
    const std::string& pcd_fn)
{
    pcl::PointCloud<pcl::PointXYZ>::Ptr in_cloud(new pcl::PointCloud<pcl::PointXYZ>);
    pcl::fromPCLPointCloud2(in_cloud_blob, *in_cloud);

    int curr_det_idx = 1;
    
    // Iterate over detections in dets3d_json
    int num_dets3d = dets3d_json["detections"].size();
    json dets3d = dets3d_json["detections"];

    for (int i = 0; i < num_dets3d; i++)
    {
        // Get the bounding box
        json bbox = dets3d[i]["bbox"];

        // Create CropBox for the box
        pcl::CropBox<pcl::PointXYZ> crop_box;
        crop_box.setInputCloud(in_cloud);

        // Set paramaters
        json bbox_pos = bbox["position"]["position"];
        json bbox_ori = bbox["position"]["orientation"];
        json bbox_size = bbox["size"];

        Eigen::Vector3f translation(
            bbox_pos["x"],
            bbox_pos["y"],
            bbox_pos["z"]);

        Eigen::Quaternionf quarternion(
            bbox_ori["w"],
            bbox_ori["x"],
            bbox_ori["y"],
            bbox_ori["z"]);
        
        Eigen::Vector3f euler_angles = quarternion.toRotationMatrix().eulerAngles(0, 1, 2);

        crop_box.setTranslation(translation);
        crop_box.setRotation(euler_angles);

        Eigen::Vector4f min_point(
            -static_cast<float>(bbox_size["x"]) / 2.0,
            -static_cast<float>(bbox_size["y"]) / 2.0,
            0.0,
            0.0);

        Eigen::Vector4f max_point(
            static_cast<float>(bbox_size["x"]) / 2.0,
            static_cast<float>(bbox_size["y"]) / 2.0,
            static_cast<float>(bbox_size["z"]),
            0.0);
        
        crop_box.setMin(min_point);
        crop_box.setMax(max_point);

        // Filter and save to out cloud
        pcl::PointCloud<pcl::PointXYZ> out_cloud;
        crop_box.filter(out_cloud);

        std::string label = dets3d[i]["label"];

        std::stringstream out_pcd_name;
        out_pcd_name << label << "-" <<
            pcd_fn.substr(0, pcd_fn.size() - 4) << "-" << curr_det_idx << ".pcd";

        std::stringstream out_pcd_path;
        out_pcd_path << out_folder_pcd_ << "/" << out_pcd_name.str();
        
        pcl::io::savePCDFileASCII(out_pcd_path.str(), out_cloud);

        std::cout << "Extracted: " << out_pcd_name.str() << std::endl;

        // Update index for the next object
        curr_det_idx++;

        // Add to labels_map
        labels_count_map_[label]++;
    }
}

}   // namespace dataset_generation