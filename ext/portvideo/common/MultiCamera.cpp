/*  portVideo, a cross platform camera framework
 Copyright (C) 2005-2015 Martin Kaltenbrunner <martin@tuio.org>

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "MultiCamera.h"

std::vector<CameraConfig> MultiCamera::getCameraConfigs(std::vector<CameraConfig> child_cams)
{
    //there should be only equal, desired cams connected
    std::vector<CameraConfig> cfg_list;

    if(child_cams.size() <= 1)
        return cfg_list;

    std::vector<std::vector<CameraConfig>>::iterator iter_grp;
    std::vector<CameraConfig>::iterator iter;
    std::vector<std::vector<CameraConfig>> grp_cams;

    //Group compatible cams
    for(iter = child_cams.begin(); iter != child_cams.end(); iter++)
    {
        CameraConfig cam_cfg = *iter;

        bool grp_found = false;
        for(iter_grp = grp_cams.begin(); iter_grp != grp_cams.end(); iter_grp++)
        {
            CameraConfig first_cam_in_grp = *(*iter_grp).begin();

            if(first_cam_in_grp.cam_width == cam_cfg.cam_width && first_cam_in_grp.cam_height == cam_cfg.cam_height
                && first_cam_in_grp.cam_fps == cam_cfg.cam_fps && first_cam_in_grp.cam_format == cam_cfg.cam_format
                && first_cam_in_grp.driver == cam_cfg.driver)
            {
                (*iter_grp).push_back(cam_cfg);
                grp_found = true;
            }
        }

        if(!grp_found)
        {
            std::vector<CameraConfig> grp;
            grp.push_back(cam_cfg);
            grp_cams.push_back(grp);
        }
    }

    //Create possible multicam configurations
    for(iter_grp = grp_cams.begin(); iter_grp != grp_cams.end(); iter_grp++)
    {
        int cams_cnt = (*iter_grp).size();
        CameraConfig first_child_in_grp = *(*iter_grp).begin();

        //foreach possible layout
        for(int rows = 1; rows <= cams_cnt; rows++)
        {
            if(cams_cnt % rows != 0)
                continue;

            int cols = cams_cnt / (float)rows;

            CameraConfig cfg;
            CameraTool::initCameraConfig(&cfg);
            cfg.driver = DRIVER_MUTLICAM;
            sprintf(cfg.name, "MultiCam (%ix%i)", rows, cols);
            cfg.cam_width = first_child_in_grp.cam_width * rows;
            cfg.cam_height = first_child_in_grp.cam_height * cols;
            cfg.cam_fps = first_child_in_grp.cam_fps;
            cfg.cam_format = first_child_in_grp.cam_format;
            
            cfg.childs.insert(cfg.childs.end(), (*iter_grp).begin(), (*iter_grp).end());
            cfg_list.push_back(cfg);
        }
    }
    
    return cfg_list;
}

CameraEngine* MultiCamera::getCamera(CameraConfig* cam_cfg)
{
	MultiCamera *cam = new MultiCamera(cam_cfg);
	return cam;
}

MultiCamera::MultiCamera(CameraConfig *cam_cfg) : CameraEngine(cam_cfg) 
{
    std::vector<CameraConfig>::iterator iter;
    for(iter = cam_cfg->childs.begin(); iter != cam_cfg->childs.end(); iter++)
    {
        if(iter->color) iter->buf_format = FORMAT_RGB;
        else iter->buf_format = FORMAT_GRAY;
    }

	frm_buffer = NULL;
	cam_buffer = NULL;
}

MultiCamera::~MultiCamera() {
	
	std::vector<CameraEngine*>::iterator iter;
	for(iter = cameras_.begin(); iter != cameras_.end(); iter++)
		delete *iter;
	
	if (cam_buffer)
	{
		delete[] cam_buffer;
		cam_buffer = NULL;
	}
}

void MultiCamera::printInfo()
{
	printf("Multicam: %i childs\n", (int)cameras_.size());
	
	std::vector<CameraEngine*>::iterator iter;
	for(iter = cameras_.begin(); iter != cameras_.end(); iter++)
	{
		(*iter)->printInfo();
		printf("\n");
	}
}

bool MultiCamera::initCamera()
{
    if(cfg == NULL || cfg->childs.size() <= 0)
    {
        std::cout << "no child cameras spezified in multicam.xml" << std::endl;
        return false;
    }

    CameraConfig first_cfg = *cfg->childs.begin();
    cameras_columns_ = cfg->cam_width / first_cfg.cam_width;
    cameras_rows_ = cfg->cam_height / first_cfg.cam_height;

    if(cameras_columns_ * cameras_rows_ != cfg->childs.size())
    {
        std::cout << "bad multicam layout" << std::endl;
        return false;
    }
	
	std::vector<CameraConfig>::iterator iter;
    for(iter = cfg->childs.begin(); iter != cfg->childs.end(); iter++)
    {
        CameraConfig* child_cfg = &(*iter);
        if(child_cfg->driver == DRIVER_MUTLICAM)
        {
            std::cout << "cascading multicam driver not supported" << std::endl;
            return false;
        }

        if(child_cfg->cam_height != first_cfg.cam_height)
        {
            std::cout << "bad multicam layout: different heights per child" << std::endl;
            return false;
        }

        if(child_cfg->cam_width != first_cfg.cam_width)
        {
            std::cout << "bad multicam layout: different widths per child" << std::endl;
            return false;
        }

        if(child_cfg->cam_format != first_cfg.cam_format)
        {
            std::cout << "bad multicam layout: different formats per cam" << std::endl;
            return false;
        }
    }

	for(iter = cfg->childs.begin(); iter != cfg->childs.end(); iter++)
	{
        CameraConfig* child_cfg = &(*iter);
		CameraEngine* cam = CameraTool::getCamera(child_cfg, false);
		if(cam == NULL)
		{
			std::cout << "error creating child cam" << child_cfg->device << std::endl;
			return false;
		}
		
		if(!cam->initCamera())
		{
			delete cam;
			return false;
		}
		
        cameras_.push_back(cam);
	}

	cfg->frame = false;
	setupFrame();
	
	if(!cam_buffer)
		cam_buffer = new unsigned char[cfg->frame_height * cfg->frame_width * cfg->buf_format];

	return true;
}

bool MultiCamera::closeCamera()
{
	bool result = true;
	
	std::vector<CameraEngine*>::iterator iter;
	for(iter = cameras_.begin(); iter != cameras_.end(); iter++)
	{
		if((*iter) == NULL) continue;
		
		result &= (*iter)->closeCamera();
	}
	
	if(cam_buffer)
	{
		delete[] cam_buffer;
		cam_buffer = NULL;
	}
	
	return result;
}

bool MultiCamera::startCamera()
{
	bool result = true;
	
	std::vector<CameraEngine*>::iterator iter;
	for(iter = cameras_.begin(); iter != cameras_.end(); iter++)
		result &= (*iter)->startCamera();
	
	if(!result)
		stopCamera();
	
	running = result;
	
	return result;
}

bool MultiCamera::stopCamera()
{
	bool result = true;
	
	std::vector<CameraEngine*>::iterator iter;
	for(iter = cameras_.begin(); iter != cameras_.end(); iter++)
		result &= (*iter)->stopCamera();
	
	running = !result;
	
	return result;
}

bool MultiCamera::resetCamera()
{
	bool result = true;
	
	std::vector<CameraEngine*>::iterator iter;
	for(iter = cameras_.begin(); iter != cameras_.end(); iter++)
		result &= (*iter)->resetCamera();
	
	return result;
}

unsigned char* MultiCamera::getFrame()
{
	std::vector<CameraEngine*>::iterator iter;
	unsigned char* childcam_buffer_start = cam_buffer;
	int grid_column = 0;
	int grid_row = 0;
	for(iter = cameras_.begin(); iter != cameras_.end(); iter++)
	{
		CameraEngine* cam = *iter;
		
		int line_size = cam->getWidth() * cam->getFormat();
		
		unsigned char* buffer_write = childcam_buffer_start;
		unsigned char* child_frame = cam->getFrame();
		if(child_frame == NULL)
		{
			this->running = false;
			return NULL;
		}
		
		for(int i = 0; i < cam->getHeight(); i++)
		{
			memcpy(buffer_write, child_frame, line_size);
			child_frame += line_size;
			buffer_write += line_size * cameras_columns_;
		}
		
		if(cameras_columns_ <= 1 || grid_column == cameras_columns_ - 1)
		{
			grid_column = 0;
			grid_row++;
			childcam_buffer_start = cam_buffer + cam->getWidth() * cameras_columns_ * cam->getHeight() * grid_row * cam->getFormat();
		}
		else
		{
			grid_column++;
			childcam_buffer_start += line_size;
		}
			
	}
	
	return cam_buffer;
}

int MultiCamera::getCameraSettingStep(int mode) { return cameras_[0]->getCameraSettingStep(mode); }
int MultiCamera::getCameraSetting(int mode) { return cameras_[0]->getCameraSetting(mode); }
int MultiCamera::getMaxCameraSetting(int mode) { return cameras_[0]->getMaxCameraSetting(mode); }
int MultiCamera::getMinCameraSetting(int mode) { return cameras_[0]->getMinCameraSetting(mode); }
bool MultiCamera::getCameraSettingAuto(int mode) { return cameras_[0]->getCameraSettingAuto(mode); }
int MultiCamera::getDefaultCameraSetting(int mode) { return cameras_[0]->getDefaultCameraSetting(mode); }
bool MultiCamera::hasCameraSetting(int mode) { return cameras_[0]->hasCameraSetting(mode); }
bool MultiCamera::hasCameraSettingAuto(int mode) { return cameras_[0]->hasCameraSettingAuto(mode); }

bool MultiCamera::setCameraSettingAuto(int mode, bool flag) 
{
	bool result = true;
	std::vector<CameraEngine*>::iterator iter;
	for(iter = cameras_.begin(); iter != cameras_.end(); iter++)
		result &= (*iter)->setCameraSettingAuto(mode, flag);
	return result;
}

bool MultiCamera::setCameraSetting(int mode, int value)
{ 
	bool result = true;
	std::vector<CameraEngine*>::iterator iter;
	for(iter = cameras_.begin(); iter != cameras_.end(); iter++)
		result &= (*iter)->setCameraSetting(mode, value);
	return result;
}

bool MultiCamera::setDefaultCameraSetting(int mode)
{
	bool result = true;
	std::vector<CameraEngine*>::iterator iter;
	for(iter = cameras_.begin(); iter != cameras_.end(); iter++)
		result &= (*iter)->setDefaultCameraSetting(mode);
	return result;
}