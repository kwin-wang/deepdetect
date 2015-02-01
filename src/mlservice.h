/**
 * DeepDetect
 * Copyright (c) 2014 Emmanuel Benazera
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

#ifndef MLSERVICE_H
#define MLSERVICE_H

#include "mllibstrategy.h"
#include "mlmodel.h"
#include <string>
#include <future>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include <iostream>

namespace dd
{
  /**
   * \brief training job 
   */
  class tjob
  {
  public:
    tjob(std::future<int> &&ft,
	 const std::chrono::time_point<std::chrono::system_clock> &tstart)
      :_ft(std::move(ft)),_tstart(tstart),_status(1) {}
    tjob(tjob &&tj)
      :_ft(std::move(tj._ft)),_tstart(std::move(tj._tstart)),_status(std::move(tj._status)) {}
    ~tjob() {}

    std::future<int> _ft; /**< training job output status upon termination. */
    std::chrono::time_point<std::chrono::system_clock> _tstart; /**< date at which the training job has started*/
    int _status = 0; /**< 0: not started, 1: running, 2: finished or terminated */
  };

  /**
   * \brief main machine learning service encapsulation
   */
  template<template <class U,class V,class W> class TMLLib, class TInputConnectorStrategy, class TOutputConnectorStrategy, class TMLModel>
    class MLService : public TMLLib<TInputConnectorStrategy,TOutputConnectorStrategy,TMLModel>
  {
  public:
    /**
     * \brief machine learning service creation
     * @param sname service name
     * @param mlmodel model object
     * @param description optional string
     */
    MLService(const std::string &sname,
	      const TMLModel &mlmodel,
	      const std::string &description="")
      :TMLLib<TInputConnectorStrategy,TOutputConnectorStrategy,TMLModel>(mlmodel),_sname(sname),_description(description),_tjobs_counter(0)
      {}
    
    /**
     * \brief copy-constructor
     * @param mls ML service
     */
    MLService(MLService &&mls) noexcept
      :TMLLib<TInputConnectorStrategy,TOutputConnectorStrategy,TMLModel>(std::move(mls)),_sname(std::move(mls._sname)),_description(std::move(mls._description)),_tjobs_counter(mls._tjobs_counter.load()),_training_jobs(std::move(mls._training_jobs))
      {}
    
    /**
     * \brief destructor
     */
    ~MLService() {}

    /**
     * \brief machine learning service initialization:
     *        - init of input connector
     *        - init of output conector
     *        - init of ML library
     * @param ad root data object
     */
    void init(const APIData &ad)
    {
      this->_inputc.init(ad.getobj("parameters").getobj("input"));
      this->_outputc.init(ad.getobj("parameters").getobj("output"));
      this->init_mllib(ad.getobj("parameters").getobj("mllib"));
    }

    /**
     * \brief get info about the service
     * @return info data object
     */
    APIData info() const
    {
      APIData ad;
      ad.add("name",_sname);
      ad.add("description",_description);
      ad.add("mllib",this->_libname);
      return ad;
    }
    
    // 
    /**
     * \brief get status of the service
     *        To be surcharged in related classes
     * @return status data object
     */
    APIData status() const
    {
      APIData ad;
      ad.add("name",_sname);
      ad.add("description",_description);
      ad.add("mllib",this->_libname);
      return ad;
    }

    /**
     * \brief starts a possibly asynchronous trainin job and returns status or job number (async job).
     * @param ad root data object
     * @param out output data object
     * @return training job number if async, otherwise status upon termination
     */
    int train_job(const APIData &ad, APIData &out)
    {
      if (ad.has("async") && ad.get("async").get<bool>())
	{
	  std::lock_guard<std::mutex> lock(_tjobs_mutex);
	  std::chrono::time_point<std::chrono::system_clock> tstart = std::chrono::system_clock::now();
	  _training_jobs.emplace(++_tjobs_counter,
				 std::move(tjob(std::async(std::launch::async,
							   [this,ad,&out]{ return this->train(ad,out); }),
						tstart)));
	  return _tjobs_counter;
	}
	else 
	  {
	    int status = this->train(ad,out);
	    out.add("loss",this->_loss.load());
	    APIData ad_params_out = ad.getobj("parameters").getobj("output");
	    if (ad_params_out.has("loss_hist") && ad_params_out.get("loss_hist").get<bool>())
	      out.add("loss_hist",this->_loss_per_iter);
	    return status;
	  }
    }

    /**
     * \brief get status of an asynchronous training job
     * @param ad root data object
     * @param out output data object
     * @return 0 if OK, 1 if job not found
     */
    int training_job_status(const APIData &ad, APIData &out)
    {
      int j = ad.get("job").get<int>();
      int secs = 0;
      if (ad.has("timeout"))
	secs = ad.get("timeout").get<int>();
      APIData ad_params_out = ad.getobj("parameters").getobj("output");
      std::lock_guard<std::mutex> lock(_tjobs_mutex);
      std::unordered_map<int,tjob>::iterator hit;
      if ((hit=_training_jobs.find(j))!=_training_jobs.end())
	{
	  std::future_status status = (*hit).second._ft.wait_for(std::chrono::seconds(secs));
	  if (status == std::future_status::timeout)
	    {
	      out.add("status","running");
	      out.add("loss",this->_loss.load());
	      std::chrono::time_point<std::chrono::system_clock> trun = std::chrono::system_clock::now();
	      out.add("time",std::chrono::duration_cast<std::chrono::seconds>(trun-(*hit).second._tstart).count());
	      if (ad_params_out.has("loss_hist") && ad_params_out.get("loss_hist").get<bool>())
		out.add("loss_hist",this->_loss_per_iter);
	    }
	  else if (status == std::future_status::ready)
	    {
	      int st = (*hit).second._ft.get(); //TODO: exception handling ?
	      if (st == 0)
		out.add("status","finished");
	      else out.add("status","unknown error");
	      out.add("loss",this->_loss.load()); // XXX: beware if there was a queue, since the job has finished, there might be a new one running.
	      std::chrono::time_point<std::chrono::system_clock> trun = std::chrono::system_clock::now();
	      out.add("time",std::chrono::duration_cast<std::chrono::seconds>(trun-(*hit).second._tstart).count());
	      if (ad.has("loss_hist") && ad.get("loss_hist").get<bool>())
		out.add("loss_hist",this->_loss_per_iter);
	      _training_jobs.erase(hit);
	    }
	  return 0;
	}
      else
	{
	  return 1; // job not found
	}
    }

    /**
     * \brief terminate a training job
     * @param ad root data object
     * @param out output data object
     * @return 0 if OK, 1 if job not found
     */
    int training_job_delete(const APIData &ad, APIData &out)
    {
      int j = ad.get("job").get<int>();
      std::lock_guard<std::mutex> lock(_tjobs_mutex);
      std::unordered_map<int,tjob>::iterator hit;
      if ((hit=_training_jobs.find(j))!=_training_jobs.end())
	{
	  std::future_status status = (*hit).second._ft.wait_for(std::chrono::seconds(0));
	  if (status == std::future_status::timeout
	      && (*hit).second._status == 1) // process is running, terminate it
	    {
	      this->_tjob_running.store(false); // signals the process
	      (*hit).second._ft.wait(); // XXX: default timeout in case the process does not return ?
	      out.add("status","terminated");
	      std::chrono::time_point<std::chrono::system_clock> trun = std::chrono::system_clock::now();
	      out.add("time",std::chrono::duration_cast<std::chrono::seconds>(trun-(*hit).second._tstart).count());
	      _training_jobs.erase(hit);
	    }
	  else if ((*hit).second._status == 0)
	    {
	      out.add("status","not started");
	    }
	  return 0;
	}
      else return 1; // job not found
    }

    std::string _sname; /**< service name. */
    std::string _description; /**< optional description of the service. */

    std::mutex _tjobs_mutex; /**< mutex around training jobs. */
    std::atomic<int> _tjobs_counter = 0; /**< training jobs counter. */
    std::unordered_map<int,tjob> _training_jobs; // XXX: the futures' dtor blocks if the object is being terminated
  };
  
}

#endif
