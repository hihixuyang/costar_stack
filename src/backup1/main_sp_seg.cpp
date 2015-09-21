#include <opencv2/core/core.hpp>
#include "../include/features.h"
#include "../include/JHUDataParser.h"

std::map<std::string, int> model_name_map;

uchar color_label[11][3] = 
{ {0, 0, 0}, 
  {255, 0, 0},
  {0, 255, 0},
  {0, 0, 255},
  {255, 255, 0},
  {255, 0, 255},
  {0, 255, 255},
  {255, 128, 0},
  {255, 0, 128},
  {0, 128, 255},
  {128, 0, 255},
};   

std::vector<std::string> readMesh(std::string mesh_path, std::vector<ModelT> &model_set)
{
    boost::filesystem::path p(mesh_path);
    std::vector< std::string > ret;
    find_files(p, ".obj", ret);
    
    std::vector< std::string > valid_names;
    for(size_t i = 0 ; i < ret.size() ; i++ )
    {
        std::string model_name = ret[i].substr(0, ret[i].size()-4);
        ModelT cur_model;
        pcl::PolygonMesh::Ptr model_mesh(new pcl::PolygonMesh()); 
        pcl::io::loadPolygonFile(mesh_path + ret[i], *model_mesh); 
        pcl::PointCloud<myPointXYZ>::Ptr model_cloud(new pcl::PointCloud<myPointXYZ>()); 
        pcl::fromPCLPointCloud2(model_mesh->cloud, *model_cloud);
        cur_model.model_mesh = model_mesh;
        cur_model.model_label = model_name;
        cur_model.model_cloud = model_cloud;
            
        model_set.push_back(cur_model);
        valid_names.push_back(model_name);
    }
    return valid_names;
}

std::vector<poseT> readGT(std::string pose_path, std::string file_id)
{
    std::vector<poseT> gt_poses;
    
    boost::filesystem::path p(pose_path);
    std::vector< std::string > ret;
    find_files(p, "_"+file_id+".csv", ret);
    for(size_t i = 0 ; i < ret.size() ; i++ )
    {
        std::string model_name = ret[i].substr(0, ret[i].size()-5-file_id.size());
        readCSV(pose_path + ret[i], model_name, gt_poses);
    }
    return gt_poses;
}

pcl::PointCloud<PointT>::Ptr genSeg_all(const pcl::PointCloud<PointT>::Ptr scene, const std::vector<ModelT> &model_set, const std::vector<poseT> &gt_poses, std::map<std::string, int> &model_map)
{
    pcl::PointCloud<myPointXYZ>::Ptr scene_xyz(new pcl::PointCloud<myPointXYZ>());
    pcl::copyPointCloud(*scene, *scene_xyz);
    
    pcl::search::KdTree<myPointXYZ> tree;
    tree.setInputCloud (scene_xyz);
    
    std::vector<int> obj_labels(scene->size(), -1);
    std::vector<float> obj_dist(scene->size(), 1000);
    float T = 0.01;
    
    for(size_t k = 0 ; k < gt_poses.size() ; k++ )
    {
        std::stringstream kk;
        kk << k;

        int model_idx = -1;
        int obj_id = -1;
        for(size_t i = 0 ; i < model_set.size(); i++ ){
            if(model_set[i].model_label == gt_poses[k].model_name)
            {
                model_idx = i;
                obj_id = model_map[model_set[i].model_label];
                //std::cerr << model_set[i].model_label << " " << obj_id << std::endl;
                break;
            }
        }
        if( obj_id <= 0 )
        {
            std::cerr << "No Matching Model!" << std::endl;
            exit(0);
        }
        
        pcl::PointCloud<myPointXYZ>::Ptr buf_cloud(new pcl::PointCloud<myPointXYZ>());
        pcl::transformPointCloud(*(model_set[model_idx].model_cloud), *buf_cloud, gt_poses[k].shift, gt_poses[k].rotation);
        
        for(pcl::PointCloud<myPointXYZ>::iterator it = buf_cloud->begin() ; it < buf_cloud->end() ; it++ )
        {
            std::vector<int> idx;
            std::vector<float> dist;
    
            tree.radiusSearch(*it, T, idx, dist, buf_cloud->size());
            for( size_t j = 0 ; j < idx.size() ; j++ )
            {
                if( obj_dist[idx[j]] > dist[j] )
                {
                    obj_labels[idx[j]] = obj_id;
                    obj_dist[idx[j]] = dist[j];
                } 
            }   
        }
    }
    
    pcl::PointCloud<PointT>::Ptr seg_cloud(new pcl::PointCloud<PointT>());
    for(size_t i = 0 ; i < scene->size() ; i++ )
    {
        PointT new_pt = scene->at(i);
        if(obj_labels[i] > 0 )
            new_pt.rgba = obj_labels[i];
        else
            new_pt.rgba = 0;
        seg_cloud->push_back(new_pt);
    }
    return seg_cloud;
}


float spAcc(const pcl::PointCloud<PointT>::Ptr gt_cloud, const std::vector<pcl::PointCloud<PointT>::Ptr> segs)
{
    if( gt_cloud->empty() == true )
        return 0.0;
    
    pcl::search::KdTree<PointT> tree;
    tree.setInputCloud(gt_cloud);
    
    float T = 0.001;
    float sqrT = T*T;
    int in_seg_count = 0;
    int total_seg_count = 0;
    for( size_t i = 0 ; i < segs.size() ; i++ )
    {
        if( segs[i]->empty() == true )
            continue;
        
        std::vector<int> count(1000, 0);
        int max = -1000;
        for( pcl::PointCloud<PointT>::const_iterator it = segs[i]->begin(); it < segs[i]->end() ; it++ )
        {
            std::vector<int> indices(1);
            std::vector<float> sqr_distances(1);
            int nres = tree.nearestKSearch(*it, 1, indices, sqr_distances);
            int cur_label = gt_cloud->at(indices[0]).rgba;
            if ( nres >= 1 && sqr_distances[0] <= sqrT )
            {
                count[cur_label]++;
                if( count[cur_label] > max )
                    max = count[cur_label];
            }
            else
            {
                count[0]++;
                if( count[0] > max )
                    max = count[0];
            }
            //std::cerr << max << " ";
        }
        if( (max + 0.0) / segs[i]->size() > 0.9 )
            in_seg_count++;
        total_seg_count++;
    }
    
    
    return (in_seg_count+0.0)/total_seg_count;
}

std::vector<int> spGt(const pcl::PointCloud<PointT>::Ptr gt_cloud, const std::vector<pcl::PointCloud<PointT>::Ptr> segs)
{
    std::vector<int> tmp_labels(segs.size(), 0);
    if( gt_cloud->empty() == true )
        return tmp_labels;
    
    pcl::search::KdTree<PointT> tree;
    tree.setInputCloud(gt_cloud);
    
    float T = 0.001;
    float sqrT = T*T;
    
    //#pragma omp parallel for schedule(dynamic, 1)
    for( size_t i = 0 ; i < segs.size() ; i++ )
    {
        if( segs[i]->empty() == true )
            continue;
        
        std::vector<int> count(1000, 0);
        int max = -1000;
        int max_id = -1;
        for( pcl::PointCloud<PointT>::const_iterator it = segs[i]->begin(); it < segs[i]->end() ; it++ )
        {
            std::vector<int> indices(1);
            std::vector<float> sqr_distances(1);
            int nres = tree.nearestKSearch(*it, 1, indices, sqr_distances);
            int cur_label = gt_cloud->at(indices[0]).rgba;
            if ( nres >= 1 && sqr_distances[0] <= sqrT )
            {
                count[cur_label]++;
                if( count[cur_label] > max )
                {
                    max = count[cur_label];
                    max_id = cur_label;
                }
            }
            else
            {
                count[0]++;
                if( count[0] > max )
                {
                    max = count[0];
                    max_id = 0;
                }
            }
            //std::cerr << max << " ";
        }
        tmp_labels[i] = max_id;
    }
    
    
    return tmp_labels;
}


int main(int argc, char** argv)
{
    std::string in_path("/home/chi/JHUIT/scene/");
    std::string mesh_path("/home/chi/devel_mode/ObjRecRANSAC/data/mesh/");
    std::string out_path("tmp/");
    std::string dict_path("JHU_kmeans_dict/");
    pcl::console::parse_argument(argc, argv, "--p", in_path);
    pcl::console::parse_argument(argc, argv, "--o", out_path);
    boost::filesystem::create_directories(out_path);
    
//    int c1 = 0, c2 = 99;
//    pcl::console::parse_argument(argc, argv, "--c1", c1);
//    pcl::console::parse_argument(argc, argv, "--c2", c2);
    
/***************************************************************************************************************/
    bool view_flag = false;
    if( pcl::console::find_switch(argc, argv, "-v") == true )
    {
        omp_set_num_threads(1);
        view_flag = true;
    }
    pcl::visualization::PCLVisualizer::Ptr viewer;
    if( view_flag == true )
    {
        viewer = pcl::visualization::PCLVisualizer::Ptr (new pcl::visualization::PCLVisualizer ("3D Viewer"));
        viewer->initCameraParameters();
        viewer->addCoordinateSystem(0.1);
        viewer->setCameraPosition(0, 0, 0.1, 0, 0, 1, 0, -1, 0);
        viewer->setSize(1280, 960);
    }
/***************************************************************************************************************/
    
    setObjID(model_name_map);
    std::vector<ModelT> model_set;
    std::vector<std::string> model_names = readMesh(mesh_path, model_set);
    int model_num = model_names.size();
    
/***************************************************************************************************************/

    float radius = 0.02;
    float down_ss = 0.005;
    float ratio = 0.1;
    int layer = 7;
    int box_num = 1;
    pcl::console::parse_argument(argc, argv, "--nn", box_num);
    pcl::console::parse_argument(argc, argv, "--rd", radius);
    pcl::console::parse_argument(argc, argv, "--rt", ratio);
    pcl::console::parse_argument(argc, argv, "--ss", down_ss);
    pcl::console::parse_argument(argc, argv, "--ll", layer);
    
    int fea_dim = -1;
    Hier_Pooler hie_producer(radius);
    hie_producer.LoadDict_L0(dict_path, "200", "200");
    hie_producer.setRatio(ratio);
    
    std::vector< boost::shared_ptr<Pooler_L0> > pooler_set(layer+1);
    for( size_t i = 1 ; i < pooler_set.size() ; i++ )
    {
        boost::shared_ptr<Pooler_L0> cur_pooler(new Pooler_L0);
        cur_pooler->setHSIPoolingParams(i);
        pooler_set[i] = cur_pooler;
    }
    //Pooler_L0 genericPooler(-1);
    //genericPooler.LoadSeedsPool(dict_path+"dict_depth_L0_200.cvmat");
    
    std::cerr << "Ratio: " << ratio << std::endl;
    std::cerr << "Downsample: " << down_ss << std::endl;

/***************************************************************************************************************/  
    for( int i = 0 ; i <= 9  ; i++ )
    {
        ObjectSet train_objects, test_objects;
        readJHUInst("/home/chi/JHUIT/ht10/", train_objects, test_objects, i, i, true);
        std::cerr << "Loading Completed... " << std::endl;
        
        test_objects.clear();
        
        int train_num = train_objects[0].size();
        std::cerr << "Train " << i << " --- " << train_num << std::endl;
        if( train_num > 0 )
        {
            std::vector< sparseVec> final_train;
            #pragma omp parallel for schedule(dynamic, 1)
            for( int j = 0 ; j < train_num ; j++ )
            {
                pcl::VoxelGrid<PointT> sor;
                pcl::ExtractIndices<PointT> ext;
                ext.setNegative(false);
                
    		pcl::PointCloud<PointT>::Ptr mycloud = train_objects[0][j].cloud;
            	pcl::PointCloud<NormalT>::Ptr mycloud_normals(new pcl::PointCloud<NormalT>());
            	computeNormals(mycloud, mycloud_normals, radius);
		MulInfoT tmp_data = convertPCD(mycloud, mycloud_normals);		

                if( down_ss > 0 )
                {
                    sor.setInputCloud(tmp_data.cloud);
                    sor.setLeafSize(down_ss, down_ss, down_ss);
                    sor.filter(*tmp_data.down_cloud);
                }
                else
                    tmp_data.down_cloud = tmp_data.cloud;
                PreCloud(tmp_data, -1, false);
                std::vector<cv::Mat> main_fea = hie_producer.getHierFea(tmp_data, 0);
                if( box_num > 1 )
                {
                    std::vector<pcl::PointIndices::Ptr> inlier_set3 = CropSegs(tmp_data, tmp_data.down_cloud->size()*0.1, tmp_data.down_cloud->size()*0.3, 30);
                    std::vector<pcl::PointIndices::Ptr> inlier_set1 = CropSegs(tmp_data, tmp_data.down_cloud->size()*0.3, tmp_data.down_cloud->size()*0.5, 30);
                    std::vector<pcl::PointIndices::Ptr> inlier_set2 = CropSegs(tmp_data, tmp_data.down_cloud->size()*0.5, tmp_data.down_cloud->size()*0.8, 30);
                    std::vector<pcl::PointIndices::Ptr> inlier_set;// = CropSegs(tmp_data, tmp_data.down_cloud->size(), tmp_data.down_cloud->size(), 1);
		    inlier_set.insert(inlier_set.end(), inlier_set1.begin(), inlier_set1.end());
		    inlier_set.insert(inlier_set.end(), inlier_set2.begin(), inlier_set2.end());
                    inlier_set.insert(inlier_set.end(), inlier_set3.begin(), inlier_set3.end());
                    if( inlier_set.empty() == true )
                        continue;

                    std::vector< sparseVec> tmp_final;
                    for(std::vector<pcl::PointIndices::Ptr>::iterator it_inlier = inlier_set.begin() ; it_inlier < inlier_set.end() ; it_inlier++ )
                    {
			if( (*it_inlier)->indices.size() > 300 )
				continue;
                        pcl::PointCloud<PointT>::Ptr cur_seg(new pcl::PointCloud<PointT>());
                        // Extract the inliers
                        ext.setInputCloud (tmp_data.down_cloud);
                        ext.setIndices (*it_inlier);
                        ext.filter (*cur_seg);
                        MulInfoT cur_data = convertPCD(tmp_data.cloud, tmp_data.cloud_normals);
                        cur_data.down_cloud = cur_seg;

                        PreCloud(cur_data, -1, false);
                        std::vector<cv::Mat> cur_fea = extFea(main_fea, (*it_inlier)->indices);
                        
                        //cv::Mat final_temp = genericPooler.PoolOneDomain(cur_fea[0], cur_fea[1], 2, false);
                        cv::Mat final_temp = multiPool(pooler_set, cur_data, cur_fea);

                        if( fea_dim > 0 && final_temp.cols != fea_dim )
                        {
                            std::cerr << "Error: fea_dim > 0 && cur_final.cols != fea_dim   " << fea_dim << " " << final_temp.cols << std::endl;
                            exit(0);
                        }
                        else if( fea_dim < 0 )
		    	{
                    	    #pragma omp critical
                    	    {
                        	fea_dim = final_temp.cols;
                    	    }
		    	}
                        std::vector< sparseVec> final_sparse;
                        sparseCvMat(final_temp, final_sparse);
                        tmp_final.push_back(final_sparse[0]);
                    }
                    #pragma omp critical
                    {
                        final_train.insert(final_train.end(), tmp_final.begin(), tmp_final.end());
                    }
                }
                else if( box_num == 1 )
                {
//		    cv::Mat cur_final = genericPooler.PoolOneDomain(main_fea[2], main_fea[1], 2, true);
	            cv::Mat cur_final = multiPool(pooler_set, tmp_data, main_fea);
		    if( fea_dim > 0 && cur_final.cols != fea_dim )
                    {
                        std::cerr << "Error: fea_dim > 0 && cur_final.cols != fea_dim   " << fea_dim << " " << cur_final.cols << std::endl;
                        exit(0);
                    }
                    else if( fea_dim < 0 )
		    {
                    	#pragma omp critical
                    	{
                            fea_dim = cur_final.cols;
                    	}
		    }
                    std::vector< sparseVec> this_sparse;
                    sparseCvMat(cur_final, this_sparse);
                    #pragma omp critical
                    {
                        final_train.push_back(this_sparse[0]);
                    }
                }
            }
            std::stringstream ss;
            ss << i+1;
            
	    saveCvMatSparse(out_path + "train_"+ss.str()+"_L9.smat", final_train, fea_dim);
            final_train.clear();
        }
        train_objects.clear();
    }
    //return 1;
/***************************************************************************************************************/
    
    std::vector< std::string > prefix_set(3);
    prefix_set[2] = "office";
    prefix_set[1] = "labpod";
    prefix_set[0] = "barrett";
    
    for( int tt = 0 ; tt <= 2 ; tt++ )
    {
        std::vector< std::vector<sparseVec> > final_test(model_num+1);
	std::vector< std::vector<sparseVec> > my_train(model_num+1);
        std::vector<std::string> scene_names;
        
        for( int i = 0 ; i < 10 ; i++ )
        {
            std::stringstream ss;
            ss << i;
            scene_names.push_back(std::string (prefix_set[tt] +"_"+ ss.str()));
        }
	int counter = 0;
        for( std::vector<std::string>::iterator scene_it = scene_names.begin() ; scene_it < scene_names.end() ; scene_it++, counter++ )
        {
            std::string cur_path(in_path + *scene_it + "/");
            std::string gt_path(in_path + *scene_it + "/poses/");

            #pragma omp parallel for schedule(dynamic, 1)
            for( int i = 0 ; i <= 99 ; i++ )
            {
                pcl::VoxelGrid<PointT> sor;
                spExt sp_ext(down_ss);

                std::stringstream ss;
                ss << i;

                std::string filename(cur_path + *scene_it + "_" + ss.str() + ".pcd");
                std::cerr << filename << std::endl;

                if( exists_test(filename) == false )//|| exists_test(filename_n) == false )
                {
                    pcl::console::print_warn("Failed to Read: %s\n", filename.c_str());
                    continue;
                }
                pcl::PointCloud<PointT>::Ptr full_cloud(new pcl::PointCloud<PointT>());
                pcl::io::loadPCDFile(filename, *full_cloud);
                
                pcl::PointCloud<PointT>::Ptr cloud(new pcl::PointCloud<PointT>());
                std::vector<int> idx_ff;
                pcl::removeNaNFromPointCloud(*full_cloud, *cloud, idx_ff);
                
                pcl::PointCloud<NormalT>::Ptr cloud_normals(new pcl::PointCloud<NormalT>());
                //pcl::io::loadPCDFile(filename_n, *cloud_normals);
                if( view_flag == true )
                {
                    viewer->addPointCloud(cloud, "cloud");
                    viewer->spin();
                    viewer->removeAllPointClouds();
                    continue;
                }
                
                computeNormals(cloud, cloud_normals, radius);

                std::vector<poseT> cur_gt = readGT(gt_path, ss.str());
                if(cur_gt.empty() == true )
                    continue;
 
                pcl::PointCloud<PointT>::Ptr down_cloud(new pcl::PointCloud<PointT>());
                if( down_ss > 0 )
                {
                    sor.setInputCloud(cloud);
                    sor.setLeafSize(down_ss, down_ss, down_ss);
                    sor.filter(*down_cloud);
                }
                else
                    down_cloud = cloud;

                pcl::PointCloud<PointT>::Ptr all_gt_cloud = genSeg_all(down_cloud, model_set, cur_gt, model_name_map);

/*
                sp_ext.clear();
                sp_ext.LoadPointCloud(cloud);
                pcl::PointCloud<PointT>::Ptr down_cloud = sp_ext.getCloud();
         
                std::vector<pcl::PointCloud<PointT>::Ptr> segs = sp_ext.getSPCloud(0);
                pcl::PointCloud<PointT>::Ptr all_gt_cloud = genSeg_all(down_cloud, model_set, cur_gt, model_name_map);
                std::vector<int> segs_labels = spGt(all_gt_cloud, segs);
                
                for( size_t j = 0 ; j < segs.size() ; j++ )
                {
                    int cur_label = segs_labels[j];
                    if( segs[j]->empty() == false && cur_label > 0 )
                    {
                        MulInfoT cur_data = convertPCD(cloud, cloud_normals);
                        cur_data.down_cloud = segs[j];
                        PreCloud(cur_data, -1, false);

                        std::vector<cv::Mat> local_fea = hie_producer.getHierFea(cur_data, 0);
                        
                        cv::Mat cur_final = multiPool(pooler_set, cur_data, local_fea);
                        //cv::Mat cur_final = genericPooler.PoolOneDomain(local_fea[0], local_fea[1], 2, false);
                        
                        if( fea_dim > 0 && cur_final.cols != fea_dim )
                        {
                            std::cerr << "Error: fea_dim > 0 && cur_final.cols != fea_dim   " << fea_dim << " " << cur_final.cols << std::endl;
                            exit(0);
                        }
                        else if( fea_dim < 0 )
                            fea_dim = cur_final.cols;
                        std::vector< sparseVec> this_sparse;
                        sparseCvMat(cur_final, this_sparse);
                        #pragma omp critical
                        {
			    if( counter % 2 == 0 )
				my_train[cur_label].push_back(this_sparse[0]);
			    else
                            	final_test[cur_label].push_back(this_sparse[0]);
                        }

                        if( view_flag == true )
                        {
                            std::stringstream ss;
                            ss << j;

                            viewer->addPointCloud(segs[j], "seg"+ss.str());
                            viewer->setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_COLOR, color_label[cur_label][0]/255.0, color_label[cur_label][1]/255.0, color_label[cur_label][2]/255.0, "seg"+ss.str());
                            viewer->setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 4, "seg"+ss.str());
                        }
                    }
                }
//*/               
//* 
                std::vector<pcl::PointCloud<PointT>::Ptr> gt_segs(model_num + 1);
                for( size_t kk = 0 ; kk < gt_segs.size() ; kk++ )
                    gt_segs[kk] = pcl::PointCloud<PointT>::Ptr (new pcl::PointCloud<PointT>());
                for( size_t kk = 0 ; kk < all_gt_cloud->size(); kk++ )
                    if( all_gt_cloud->at(kk).rgba > 0 )
                        gt_segs[all_gt_cloud->at(kk).rgba]->push_back(down_cloud->at(kk));
                
                
                for( size_t j = 0 ; j < gt_segs.size() ; j++ )
                {
                    int cur_label = j;
                    if( gt_segs[j]->empty() == false && cur_label > 0 )
                    {
                        if( view_flag == true )
                        {
                            std::stringstream ss;
                            ss << j;

                            viewer->addPointCloud(gt_segs[j], "seg"+ss.str());
                            viewer->setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_COLOR, color_label[cur_label][0]/255.0, color_label[cur_label][1]/255.0, color_label[cur_label][2]/255.0, "seg"+ss.str());
                            viewer->setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 4, "seg"+ss.str());
                            continue;
                        }
                        
                        MulInfoT cur_data = convertPCD(cloud, cloud_normals);
                        cur_data.down_cloud = gt_segs[j];
                        PreCloud(cur_data, -1, false);

                        std::vector<cv::Mat> local_fea = hie_producer.getHierFea(cur_data, 0);
                        
                        cv::Mat cur_final = multiPool(pooler_set, cur_data, local_fea);
                        //cv::Mat cur_final = genericPooler.PoolOneDomain(local_fea[2], local_fea[1], 2, true);
                        
                        if( fea_dim > 0 && cur_final.cols != fea_dim )
                        {
                            std::cerr << "Error: fea_dim > 0 && cur_final.cols != fea_dim   " << fea_dim << " " << cur_final.cols << std::endl;
                            exit(0);
                        }
                        else if( fea_dim < 0 )
                            fea_dim = cur_final.cols;
                        std::vector< sparseVec> this_sparse;
                        sparseCvMat(cur_final, this_sparse);
                        //#pragma omp critical
                        //{
			//    if( counter % 2 == 0 )
			//	my_train[cur_label].push_back(this_sparse[0]);
			//    else
                        //    	final_test[cur_label].push_back(this_sparse[0]);
                        //}
                        #pragma omp critical
                        {
                            final_test[cur_label].push_back(this_sparse[0]);
                        }
                    }
                }
//*/
                if( view_flag == true )
                {
                    viewer->addPointCloud(cloud, "cloud");
                    viewer->spin();
                    viewer->removeAllPointClouds();
                }

            }
        }

        std::stringstream tt_str;
        tt_str << tt;
        for( size_t i = 0 ; i < final_test.size() ; i++ )
        {
            std::stringstream ss;
            ss << i;
            //std::cerr << "Saving Train " << i << " --- " << my_train[i].size() << std::endl;
            //if( my_train[i].empty() == false )
            //    saveCvMatSparse(out_path + "train_"+ss.str()+"_L"+tt_str.str()+".smat", my_train[i], fea_dim);
            std::cerr << "Saving Test " << i << " --- " << final_test[i].size() << std::endl;
            if( final_test[i].empty() == false )
                saveCvMatSparse(out_path + "test_"+ss.str()+"_L"+tt_str.str()+".smat", final_test[i], fea_dim);
            final_test[i].clear();
        }
    }
    std::cerr << "Fea_dim: " << fea_dim << std::endl;
    return 1;
} 




//int main(int argc, char** argv)
//{
//    std::string in_path("/home/chi/JHUIT/scene/");
//    std::string mesh_path("/home/chi/devel_mode/ObjRecRANSAC/data/mesh/");
//    std::string out_file("tmp.txt");
//    pcl::console::parse_argument(argc, argv, "--p", in_path);
//    pcl::console::parse_argument(argc, argv, "--o", out_file);
//
//    int c1 = 0, c2 = 99;
//    pcl::console::parse_argument(argc, argv, "--c1", c1);
//    pcl::console::parse_argument(argc, argv, "--c2", c2);
//    
//    float voxels = 0.008;
//    float seeds = 0.1;
//    float cw = 0.2;
//    float dw = 0.4;
//    float nw = 1.0;
//    pcl::console::parse_argument(argc, argv, "--voxels", voxels);
//    pcl::console::parse_argument(argc, argv, "--seeds", seeds);
//    pcl::console::parse_argument(argc, argv, "--cw", cw);
//    pcl::console::parse_argument(argc, argv, "--dw", dw);
//    pcl::console::parse_argument(argc, argv, "--nw", nw);
//    float down_ss = 0.005;
//    pcl::console::parse_argument(argc, argv, "--ss", down_ss);
//    
//    spExt sp_ext(down_ss);
//    sp_ext.setParams(voxels, seeds, cw, dw, nw);
//    
//    setObjID(model_name_map);
//    std::vector<ModelT> model_set;
//    std::vector<std::string> model_names = readMesh(mesh_path, model_set);
//    
//    std::ifstream fp_scenes;
//    fp_scenes.open((in_path + "test_scene.txt").c_str());
//    std::vector<std::string> scene_names;
//    while(true)
//    {
//        std::string temp;
//        if( !(fp_scenes >> temp))
//            break;
//        if( temp.empty() || temp[0] != '#' )
//        {
//            scene_names.push_back(temp);
//            std::cerr << temp << std::endl;
//        }
//    }
//    fp_scenes.close();
//    
//    
//    float avg_acc = 0;
//    int count = 0;
//    float avg_sp_num = 0;
//    for( std::vector<std::string>::iterator scene_it = scene_names.begin() ; scene_it < scene_names.end() ; scene_it++ )
//    {
//        std::string cur_path(in_path + *scene_it + "/for_recog/");
//        std::string gt_path(in_path + *scene_it + "/poses/");
//    
//        for( int i = c1 ; i <= c2 ; i++ )
//        {
//            std::stringstream ss;
//            ss << i;
//
//            std::string filename(cur_path + *scene_it + "_" + ss.str() + ".pcd");
//            std::string filename_n(cur_path + "normal_" + *scene_it + "_" + ss.str() + ".pcd");
//
//            if( exists_test(filename) == false || exists_test(filename_n) == false )
//            {
//                pcl::console::print_warn("Failed to Read: %s\n", filename.c_str());
//                continue;
//            }
//            pcl::PointCloud<PointT>::Ptr cloud(new pcl::PointCloud<PointT>());
//            pcl::PointCloud<NormalT>::Ptr cloud_normal(new pcl::PointCloud<NormalT>());
//            pcl::io::loadPCDFile(filename, *cloud);
//            pcl::io::loadPCDFile(filename_n, *cloud_normal);
//
//            std::cerr << filename << "-Loaded" << std::endl;
//            sp_ext.clear();
//            sp_ext.LoadPointCloud(cloud);
//            pcl::PointCloud<PointT>::Ptr down_cloud = sp_ext.getCloud();
//
//            std::vector<poseT> cur_gt = readGT(gt_path, ss.str());
//            if(cur_gt.empty() == true )
//                continue;
//            pcl::PointCloud<PointT>::Ptr gt_cloud = genSeg(down_cloud, model_set, cur_gt, model_name_map);
//
//            std::vector<pcl::PointCloud<PointT>::Ptr> segs_low = sp_ext.getSPCloud(0);
//            float acc = spAcc(gt_cloud, segs_low);
//            
//            std::cerr << "Acc---" << acc << std::endl;
//            avg_acc += acc;
//            avg_sp_num += segs_low.size();
//            count++;
//        }
//    }
//    
//    std::ofstream fp;
//    fp.open(out_file.c_str());
//    
//    fp << voxels << " " << seeds << " " << cw << " " << dw << " " << nw << std::endl;
//    fp << avg_sp_num / count << std::endl;
//    fp << avg_acc / count << std::endl;
//    
//    std::cerr << "Avg Sp Boundary Acc: " << avg_acc / count << std::endl;
//    return 1;
//} 
