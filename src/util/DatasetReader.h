/**
* This file is part of DSO.
* 
* Copyright 2016 Technical University of Munich and Intel.
* Developed by Jakob Engel <engelj at in dot tum dot de>,
* for more information see <http://vision.in.tum.de/dso>.
* If you use this code, please cite the respective publications as
* listed on the above website.
*
* DSO is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* DSO is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with DSO. If not, see <http://www.gnu.org/licenses/>.
*/


#pragma once
#include "util/settings.h"
#include "util/globalFuncs.h"
#include "util/globalCalib.h"

#include <sstream>
#include <fstream>
#ifndef _DSO_ON_WIN
#include <dirent.h>
#endif
#include <algorithm>

#include "util/Undistort.h"
#include "IOWrapper/ImageRW.h"

#if HAS_ZIPLIB
	#include "zip.h"
#endif

#include <boost/thread.hpp>
#include <boost/filesystem.hpp>
#include <boost/range/iterator_range.hpp>

using namespace dso;



inline int getdir (std::string dir, std::vector<std::string> &files)
{
#ifdef _DSO_ON_WIN
	using namespace boost::filesystem;

	path p(dir);

	if (is_directory(p)) {
		std::cout << p << " is a directory containing:\n";

		for (directory_entry& entry : boost::make_iterator_range(directory_iterator(p), {}))
		{
			std::cout << entry << "\n";
			if (entry.path().has_filename())
				files.push_back(entry.path().filename().string());
		}

		std::sort(files.begin(), files.end());
		if (dir.at(dir.length() - 1) != '/') dir = dir + "/";
		for (unsigned int i = 0; i < files.size(); i++)
		{
			if (files[i].at(0) != '/')
				files[i] = dir + files[i];
		}

		return files.size();
	}

	return 0;
#else
    DIR *dp;
    struct dirent *dirp;
    if((dp  = opendir(dir.c_str())) == NULL)
    {
        return -1;
    }

    while ((dirp = readdir(dp)) != NULL) {
    	std::string name = std::string(dirp->d_name);

    	if(name != "." && name != "..")
    		files.push_back(name);
    }
    closedir(dp);


    std::sort(files.begin(), files.end());

    if(dir.at( dir.length() - 1 ) != '/') dir = dir+"/";
	for(unsigned int i=0;i<files.size();i++)
	{
		if(files[i].at(0) != '/')
			files[i] = dir + files[i];
	}

    return files.size();
#endif //_DSO_ON_WIN
}


struct PrepImageItem
{
	int id;
	bool isQueud;
	ImageAndExposure* pt;

	inline PrepImageItem(int _id)
	{
		id=_id;
		isQueud = false;
		pt=0;
	}

	inline void release()
	{
		if(pt!=0) delete pt;
		pt=0;
	}
};




class ImageFolderReader
{
public:
	ImageFolderReader(std::string path, std::string depth_path, std::string calibFile, std::string gammaFile, std::string vignetteFile)
	{
		this->path = path;
		this->calibfile = calibFile;

		//@qxc62 add depth images
		this->depth_path = depth_path;
		this->hasDepth = false;

#if HAS_ZIPLIB
		ziparchive=0;
		databuffer=0;
		databufferDepth=0;
#endif

		isZipped = (path.length()>4 && path.substr(path.length()-4) == ".zip");

		//@qxc62 add depth images
		isDepthZipped = (depth_path.length() > 4 && depth_path.substr(depth_path.length() - 4) == ".zip");


		if(isZipped)
		{
#if HAS_ZIPLIB
			int ziperror=0;
			ziparchive = zip_open(path.c_str(),  ZIP_RDONLY, &ziperror);
			if(ziperror!=0)
			{
				printf("ERROR %d reading archive %s!\n", ziperror, path.c_str());
				//@qxc62 add a stop before quit
				std::cin.get();
				exit(1);
			}

			files.clear();
			int numEntries = zip_get_num_entries(ziparchive, 0);
			int record_numEntries = numEntries;
			for(int k=0;k<numEntries;k++)
			{
				const char* name = zip_get_name(ziparchive, k,  ZIP_FL_ENC_STRICT);
				std::string nstr = std::string(name);
				if(nstr == "." || nstr == "..") continue;
				if (nstr.back() == '/')
				{
					record_numEntries--;
				}
				else
				{
					files.push_back(name);
				}
				//@qxc62 debug info
				//std::cout << name << std::endl;
			}

			printf("got %d entries and %d files!\n", record_numEntries, (int)files.size());
			std::sort(files.begin(), files.end());
#else
			printf("ERROR: cannot read .zip archive, as compile without ziplib!\n");
			//@qxc62 add a stop before quit
			std::cin.get();
			exit(1);
#endif
		}
		else
			getdir (path, files);

		//@qxc62 add depth images
		if (isDepthZipped)
		{
#if HAS_ZIPLIB
			int ziperror = 0;
			ziparchiveDepth = zip_open(depth_path.c_str(), ZIP_RDONLY, &ziperror);
			if (ziperror != 0)
			{
				printf("ERROR %d reading archive %s!\n", ziperror, depth_path.c_str());
				//@qxc62 add a stop before quit
				std::cin.get();
				exit(1);
			}

			depth_files.clear();
			int numEntries = zip_get_num_entries(ziparchiveDepth, 0);
			int record_numEntries = numEntries;
			for (int k = 0; k < numEntries; k++)
			{
				const char* name = zip_get_name(ziparchiveDepth, k, ZIP_FL_ENC_STRICT);
				std::string nstr = std::string(name);
				if (nstr == "." || nstr == "..") continue;
				if (nstr.back() == '/')
				{
					record_numEntries--;
				}
				else
				{
					depth_files.push_back(name);
				}
				
				//@qxc62 debug info
				//std::cout << name << std::endl;
			}

			printf("got %d entries and %d files!\n", record_numEntries, (int)depth_files.size());
			std::sort(depth_files.begin(), depth_files.end());
#else
			printf("ERROR: cannot read .zip archive, as compile without ziplib!\n");
			//@qxc62 add a stop before quit
			std::cin.get();
			exit(1);
#endif
		}
		else
			getdir(depth_path, depth_files);

		//@qxc62 add depth images
		if(depth_files.size()>0)
		{
			if (files.size() != depth_files.size())
			{
				printf("ERROR: the number of depth map is %d , not match the number of images %d!\n", (int)depth_files.size(), (int)files.size());
				//@qxc62 add a stop before quit
				std::cin.get();
				exit(1);
			}
			else
				hasDepth = true;
		}

		undistort = Undistort::getUndistorterForFile(calibFile, gammaFile, vignetteFile);


		widthOrg = undistort->getOriginalSize()[0];
		heightOrg = undistort->getOriginalSize()[1];
		width=undistort->getSize()[0];
		height=undistort->getSize()[1];


		// load timestamps if possible.
		loadTimestamps();
		printf("ImageFolderReader: got %d images in %s!\n", (int)files.size(), path.c_str());

		//@qxc62 add depth images
		if(hasDepth)
			printf("ImageFolderReader: got %d image depth maps in %s!\n", (int)depth_files.size(), depth_path.c_str());

	}
	~ImageFolderReader()
	{
#if HAS_ZIPLIB
		if(ziparchive!=0) zip_close(ziparchive);
		if(databuffer!=0) delete databuffer;
		//@qxc62 add depth images
		if (ziparchiveDepth != 0) zip_close(ziparchiveDepth);
		if (databufferDepth != 0) delete databufferDepth;
#endif


		delete undistort;
	};

	Eigen::VectorXf getOriginalCalib()
	{
		return undistort->getOriginalParameter().cast<float>();
	}
	Eigen::Vector2i getOriginalDimensions()
	{
		return  undistort->getOriginalSize();
	}

	void getCalibMono(Eigen::Matrix3f &K, int &w, int &h)
	{
		K = undistort->getK().cast<float>();
		w = undistort->getSize()[0];
		h = undistort->getSize()[1];
	}

	void setGlobalCalibration()
	{
		int w_out, h_out;
		Eigen::Matrix3f K;
		getCalibMono(K, w_out, h_out);
		setGlobalCalib(w_out, h_out, K);
	}

	int getNumImages()
	{
		return files.size();
	}

	double getTimestamp(int id)
	{
		if(timestamps.size()==0) return id*0.1f;
		if(id >= (int)timestamps.size()) return 0;
		if(id < 0) return 0;
		return timestamps[id];
	}


	void prepImage(int id, bool as8U=false)
	{

	}


	MinimalImageB* getImageRaw(int id)
	{
			return getImageRaw_internal(id,0);
	}

	ImageAndExposure* getImage(int id, bool forceLoadDirectly=false)
	{
		return getImage_internal(id, 0);
	}


	inline float* getPhotometricGamma()
	{
		if(undistort==0 || undistort->photometricUndist==0) return 0;
		return undistort->photometricUndist->getG();
	}


	// undistorter. [0] always exists, [1-2] only when MT is enabled.
	Undistort* undistort;
private:


	MinimalImageB* getImageRaw_internal(int id, int unused)
	{
		if(!isZipped)
		{
			// CHANGE FOR ZIP FILE
			return IOWrap::readImageBW_8U(files[id]);
		}
		else
		{
#if HAS_ZIPLIB
			if(databuffer==0) databuffer = new char[widthOrg*heightOrg*6+10000];
			zip_file_t* fle = zip_fopen(ziparchive, files[id].c_str(), 0);
			long readbytes = zip_fread(fle, databuffer, (long)widthOrg*heightOrg*6+10000);

			//@qxc62 debug info
			//std::cout << files[id].c_str() << std::endl;

			if(readbytes > (long)widthOrg*heightOrg*6)
			{
				printf("read %ld/%ld bytes for file %s. increase buffer!!\n", readbytes,(long)widthOrg*heightOrg*6+10000, files[id].c_str());
				delete[] databuffer;
				databuffer = new char[(long)widthOrg*heightOrg*30];
				fle = zip_fopen(ziparchive, files[id].c_str(), 0);
				readbytes = zip_fread(fle, databuffer, (long)widthOrg*heightOrg*30+10000);

				if(readbytes > (long)widthOrg*heightOrg*30)
				{
					printf("buffer still to small (read %ld/%ld). abort.\n", readbytes,(long)widthOrg*heightOrg*30+10000);
					//@qxc62 add a stop before quit
					std::cin.get();
					exit(1);
				}
			}

			return IOWrap::readStreamBW_8U(databuffer, readbytes);
#else
			printf("ERROR: cannot read .zip archive, as compile without ziplib!\n");
			//@qxc62 add a stop before quit
			std::cin.get();
			exit(1);
#endif
		}
	}

	MinimalImageB* getDepth_internal(int id, int unused)
	{
		if (!isDepthZipped)
		{
			// CHANGE FOR ZIP FILE
			return IOWrap::readImageBW_8U(depth_files[id]);
		}
		else
		{
#if HAS_ZIPLIB
			if (databufferDepth == 0) databufferDepth = new char[widthOrg * heightOrg * 6 + 10000];
			zip_file_t* fle = zip_fopen(ziparchiveDepth, depth_files[id].c_str(), 0);
			long readbytes = zip_fread(fle, databufferDepth, (long)widthOrg * heightOrg * 6 + 10000);

			//@qxc62 debug info
			//std::cout << depth_files[id].c_str() << std::endl;

			if (readbytes > (long)widthOrg * heightOrg * 6)
			{
				printf("read %ld/%ld bytes for file %s. increase buffer!!\n", readbytes, (long)widthOrg * heightOrg * 6 + 10000, depth_files[id].c_str());
				delete[] databufferDepth;
				databufferDepth = new char[(long)widthOrg * heightOrg * 30];
				fle = zip_fopen(ziparchiveDepth, files[id].c_str(), 0);
				readbytes = zip_fread(fle, databufferDepth, (long)widthOrg * heightOrg * 30 + 10000);

				if (readbytes > (long)widthOrg * heightOrg * 30)
				{
					printf("buffer still to small (read %ld/%ld). abort.\n", readbytes, (long)widthOrg * heightOrg * 30 + 10000);
					//@qxc62 add a stop before quit
					std::cin.get();
					exit(1);
				}
			}

			return IOWrap::readStreamBW_8U(databufferDepth, readbytes);
#else
			printf("ERROR: cannot read .zip archive, as compile without ziplib!\n");
			//@qxc62 add a stop before quit
			std::cin.get();
			exit(1);
#endif
		}
	}


	ImageAndExposure* getImage_internal(int id, int unused)
	{
		MinimalImageB* minimg = getImageRaw_internal(id, 0);
		ImageAndExposure* ret2 = undistort->undistort<unsigned char>(
				minimg,
				(exposures.size() == 0 ? 1.0f : exposures[id]),
				(timestamps.size() == 0 ? 0.0 : timestamps[id]));

		//@qxc62 add depth images
		if(hasDepth)
		{
			MinimalImageB* minimgDepth = getDepth_internal(id, 0);
			ImageAndExposure* retDepth = undistort->undistort<unsigned char>(
				minimgDepth,
				(exposures.size() == 0 ? 1.0f : exposures[id]),
				(timestamps.size() == 0 ? 0.0 : timestamps[id]));
			memcpy(ret2->depth, retDepth->image, retDepth->w * retDepth->h * sizeof(float));
			ret2->hasDepth = true;
		}

		delete minimg;
		return ret2;
	}

	inline void loadTimestamps()
	{
		std::ifstream tr;
		std::string timesFile = path.substr(0,path.find_last_of('/')) + "/times.txt";
		tr.open(timesFile.c_str());
		while(!tr.eof() && tr.good())
		{
			std::string line;
			char buf[1000];
			tr.getline(buf, 1000);

			int id;
			double stamp;
			float exposure = 0;

			if(3 == sscanf(buf, "%d %lf %f", &id, &stamp, &exposure))
			{
				timestamps.push_back(stamp);
				exposures.push_back(exposure);
			}

			else if(2 == sscanf(buf, "%d %lf", &id, &stamp))
			{
				timestamps.push_back(stamp);
				exposures.push_back(exposure);
			}
		}
		tr.close();

		// check if exposures are correct, (possibly skip)
		bool exposuresGood = ((int)exposures.size()==(int)getNumImages()) ;
		for(int i=0;i<(int)exposures.size();i++)
		{
			if(exposures[i] == 0)
			{
				// fix!
				float sum=0,num=0;
				if(i>0 && exposures[i-1] > 0) {sum += exposures[i-1]; num++;}
				if(i+1<(int)exposures.size() && exposures[i+1] > 0) {sum += exposures[i+1]; num++;}

				if(num>0)
					exposures[i] = sum/num;
			}

			if(exposures[i] == 0) exposuresGood=false;
		}


		if((int)getNumImages() != (int)timestamps.size())
		{
			printf("set timestamps and exposures to zero!\n");
			exposures.clear();
			timestamps.clear();
		}

		if((int)getNumImages() != (int)exposures.size() || !exposuresGood)
		{
			printf("set EXPOSURES to zero!\n");
			exposures.clear();
		}

		printf("got %d images and %d timestamps and %d exposures.!\n", (int)getNumImages(), (int)timestamps.size(), (int)exposures.size());
	}




	std::vector<ImageAndExposure*> preloadedImages;
	std::vector<std::string> files;

	//@qxc62 add depth images
	std::vector<std::string> depth_files;
	std::vector<double> timestamps;
	std::vector<float> exposures;

	int width, height;
	int widthOrg, heightOrg;

	std::string path;
	std::string calibfile;

	//@qxc62 add depth images
	std::string depth_path;
	bool hasDepth;

	bool isZipped;

	//@qxc62 add depth images
	bool isDepthZipped;

#if HAS_ZIPLIB
	zip_t* ziparchive;
	char* databuffer;

	//@qxc62 add depth images
	zip_t* ziparchiveDepth;
	char* databufferDepth;
#endif
};

