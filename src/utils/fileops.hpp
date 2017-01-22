/**
 * DeepDetect
 * Copyright (c) 2014-2015 Emmanuel Benazera
 * Author: Emmanuel Benazera <beniz@droidnik.fr>
 *
 * This file is part of deepdetect.
 *
 * deepdetect is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * deepdetect is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with deepdetect.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef DD_FILEOPS_H
#define DD_FILEOPS_H

#include <dirent.h>
#include <fstream>
#include <unordered_set>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <vector>
#include <iterator>
#include <algorithm>
#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;

namespace dd
{
  // TODO(yajun): implement all method using boost::filesystem
  // which more readable
  class fileops
  {
  public:

    static bool file_exists(const std::string &fname)
    {
      return fs::exists(fs::path(fname));
    }

    static bool file_exists(const std::string &fname,
			    bool &directory)
    {
      directory = false;
      bool ret = false;
      fs::path p = fs::path(fname);
      try {
        if (fs::exists(p)) {
          directory = fs::is_directory(p);
          ret = true;
        }
      } catch (const fs::filesystem_error& e) {
        directory = false;
      }
      return ret;
    }
    
    static long int file_last_modif(const std::string &fname)
    {
      try {
        return fs::last_write_time(fs::path(fname));
      } catch (const fs::filesystem_error& e) {
        return -1;
      }
    }

    // fix issue #119: https://github.com/beniz/deepdetect/issues/119
    static int list_directory(const std::string &repo,
			      const bool &files,
			      const bool &dirs,
			      std::unordered_set<std::string> &lfiles)
    {
      typedef std::vector<fs::path> vec;

      fs::path p = fs::path(repo);
      try {
        if(!(fs::exists(p) && fs::is_directory(p)))
        {
          return 1;
        }

        vec v;
        std::copy_if(fs::directory_iterator(p), fs::directory_iterator(), std::back_inserter(v),
                     [files, dirs](const fs::path& pp){
          return (files && fs::is_regular_file(pp)) || (dirs && fs::is_directory(pp));
        });

        std::transform(v.begin(), v.end(), std::inserter(lfiles, lfiles.begin()), [](const fs::path& pp){
          return pp.string();
        });

        return 0;
      } catch (const fs::filesystem_error& e) {
        return 1;
      }
    }

    // remove everything, including first level directories within directory
    static int clear_directory(const std::string &repo)
    {
      try {
        return fs::remove_all(fs::path(repo));

      } catch (const fs::filesystem_error& e) {
        return 1;
      }
    }

    // empty extensions means a wildcard
    static int remove_directory_files(const std::string &repo,
				      const std::vector<std::string> &extensions)
    {
      int err = 0;
      DIR *dir;
      struct dirent *ent;
      if ((dir = opendir(repo.c_str())) != NULL) {
	while ((ent = readdir(dir)) != NULL) 
	  {
	    std::string lf = std::string(ent->d_name);
	    if (ent->d_type == DT_DIR && ent->d_name[0] == '.')
	      continue;
	    if (extensions.empty())
	      {
		std::string f = std::string(repo) + "/" + lf;
		err += remove(f.c_str());
	      }
	    else
	      {
		for (std::string s : extensions)
		  if (lf.find(s) != std::string::npos)
		    {
		      std::string f = std::string(repo) + "/" + lf;
		      err += remove(f.c_str());
		      break;
		    }
	      }
	  }
	closedir(dir);
	return err;
      } 
      else 
	{
	  return 1;
	}
    }

    // Note: the synopsis of copy_file has been changed.
    // the old synopsis is:
    //    it will return 1 if the fin cannot be open
    //    it will return 2 if the fout cannot be open
    //    it will return 0 if successfully invoke this function
    // yajun 2017/01/22
    static int copy_file(const std::string &fin,
			 const std::string &fout)
    {
      auto p_in = fs::path(fin);
      auto p_out = fs::path(fout);
      try {
        if (fs::is_directory(p_in)) {
          return 1;
        }

        fs::copy(fs::path(fin), fs::path(fout));
        return 0;
      } catch (const fs::filesystem_error& e) {
        return 1;
      }
    }

    static int remove_file(const std::string &repo,
			   const std::string &f)
    {
      try {
        auto full_path = fs::path(repo) / fs::path(f);
        fs::remove(full_path);
        return 0;
      } catch (const fs::filesystem_error& e) {
        return -1;
      }
    }
    
  };
  
}

#endif
