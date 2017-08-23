#include "eudaq/DataConverterPlugin.hh"
#include "eudaq/StandardEvent.hh"
#include "eudaq/Utils.hh"

// All LCIO-specific parts are put in conditional compilation blocks
// so that the other parts may still be used if LCIO is not available.
#if USE_LCIO
#include "IMPL/LCEventImpl.h"
#include "IMPL/TrackerRawDataImpl.h"
#include "IMPL/LCCollectionVec.h"
#include "lcio.h"
#endif

namespace eudaq {

  // The event type for which this converter plugin will be registered
  // Modify this to match your actual event type (from the Producer)
  static const char *EVENT_TYPE = "Ph2Event";

  // Declare a new class that inherits from DataConverterPlugin
  class Ph2ACFConverterPlugin : public DataConverterPlugin {

  public:
    // This is called once at the beginning of each run.
    virtual void Initialize(const Event &bore, const Configuration &cnf) {

#ifndef WIN32 // some linux Stuff //$$change
      (void)cnf; // just to suppress a warning about unused parameter cnf
#endif

    }

    // This should return the trigger ID (as provided by the TLU)
    // if it was read out, otherwise it can either return (unsigned)-1,
    // or be left undefined as there is already a default version.
    virtual unsigned GetTriggerID(const Event &ev) const {      
      return (unsigned) ev.GetTag("TLU_TRIGGER_ID", -1);
    }

    // Here, the data from the RawDataEvent is extracted into a StandardEvent.
    // The return value indicates whether the conversion was successful.
    // Again, this is just an example, adapted it for the actual data layout.
    virtual bool GetStandardSubEvent(StandardEvent &sev,
                                     const Event &ev) const {

      std::string sensortype = "CBC";
      if(const RawDataEvent * cRawEvent = dynamic_cast<const RawDataEvent* > (&ev))
      {
          uint32_t cNBlocks = cRawEvent->NumBlocks();
          for(uint32_t sensor_id = 0; sensor_id < cNBlocks; sensor_id++)
          {
              StandardPlane plane(sensor_id, EVENT_TYPE, sensortype);
              // Set the number of pixels
              int width = getlittleendian<unsigned short>(&cRawEvent->GetBlock(sensor_id)[WIDTH_OFFSET]);
              int height = getlittleendian<unsigned short>(&cRawEvent->GetBlock(sensor_id)[HEIGHT_OFFSET]);
              plane.SetSizeRaw(width, height);
              // Set the trigger ID
              plane.SetTLUEvent(GetTriggerID(ev));

              //get strip data
              uint32_t value_offset = DATA_OFFSET;
              for(uint32_t ch = 0; ch < width; ch++ )
              {
                  plane.PushPixel(ch,0,getlittleendian<unsigned short>(&cRawEvent->GetBlock(sensor_id)[value_offset]));
                  value_offset += 2;
              }

              // Add the plane to the StandardEvent
              sev.AddPlane(plane);
          }

          // Indicate that data was successfully converted
          return true;
      }
      else
      {
          return false;
      }
    }

#if USE_LCIO
    // This is where the conversion to LCIO is done
    virtual lcio::LCEvent *GetLCIOEvent(const Event * /*ev*/) const {
      return 0;
    }
#endif

  private:
    // The constructor can be private, only one static instance is created
    // The DataConverterPlugin constructor must be passed the event type
    // in order to register this converter for the corresponding conversions
    // Member variables should also be initialized to default values here.
    Ph2ACFConverterPlugin()
        : DataConverterPlugin(EVENT_TYPE) {}

    // The single instance of this converter plugin
    static Ph2ACFConverterPlugin m_instance;

    // offsets
    static const unsigned WIDTH_OFFSET = 0;
    static const unsigned HEIGHT_OFFSET = 2;
    static const unsigned DATA_OFFSET = 6;

  }; // class ExampleConverterPlugin

  // Instantiate the converter plugin instance
  Ph2ACFConverterPlugin Ph2ACFConverterPlugin::m_instance;

} // namespace eudaq
