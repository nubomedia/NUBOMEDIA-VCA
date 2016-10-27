
/* Autogenerated with kurento-module-creator */

#ifndef __NUBO_NOSE_DETECTOR_IMPL_HPP__
#define __NUBO_NOSE_DETECTOR_IMPL_HPP__

#include "FilterImpl.hpp"
#include "NuboNoseDetector.hpp"
#include <EventHandler.hpp>
#include <boost/property_tree/ptree.hpp>

namespace kurento
{
  namespace module
  {
    namespace nubonosedetector
    {
      class NuboNoseDetectorImpl;
    } /* nubonosedetector */
  } /* module */
} /* kurento */

namespace kurento
{
  void Serialize (std::shared_ptr<kurento::module::nubonosedetector::NuboNoseDetectorImpl> &object, JsonSerializer &serializer);
} /* kurento */

namespace kurento
{
  class MediaPipelineImpl;
} /* kurento */

namespace kurento
{
  namespace module
  {
    namespace nubonosedetector
    {

      class NuboNoseDetectorImpl : public FilterImpl, public virtual NuboNoseDetector
      {

      public:

	NuboNoseDetectorImpl (const boost::property_tree::ptree &config, std::shared_ptr<MediaPipeline> mediaPipeline);

	virtual ~NuboNoseDetectorImpl ();

	void showNoses (int viewNoses);
	void detectByEvent(int event);
	void sendMetaData(int metaData);
	void multiScaleFactor(int scaleFactor);
	void processXevery4Frames(int xper4);
	void widthToProcess(int width);
	void activateServerEvents (int activate,int ms);
        void unsetOverlayedImage ();
        void setOverlayedImage (const std::string &uri, float offsetXPercent, float offsetYPercent, float widthPercent, float heightPercent);

	/* Next methods are automatically implemented by code generator */
	sigc::signal<void, OnNose> signalOnNose;
	virtual bool connect (const std::string &eventType, std::shared_ptr<EventHandler> handler);
	virtual void invoke (std::shared_ptr<MediaObjectImpl> obj,
			     const std::string &methodName, const Json::Value &params,
			     Json::Value &response);

	virtual void Serialize (JsonSerializer &serializer);

      protected: 
	virtual void postConstructor ();

      private:
	GstElement *nubo_nose = NULL;
	gulong handlerOnNoseEvent = 0;
	void onNose (gchar *);
	void split_message (std::string fi, std::string delimiter, std::vector<std::string> *v);
	
	class StaticConstructor
	{
	public:
	  StaticConstructor();
	};

	static StaticConstructor staticConstructor;

      };

    } /* nubonosedetector */
  } /* module */
} /* kurento */

#endif /*  __NUBO_NOSE_DETECTOR_IMPL_HPP__ */
