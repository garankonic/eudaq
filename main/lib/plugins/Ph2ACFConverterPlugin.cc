#include "eudaq/DataConverterPlugin.hh"
#include "eudaq/StandardEvent.hh"
#include "eudaq/RawDataEvent.hh"
#include "eudaq/Utils.hh"

// All LCIO-specific parts are put in conditional compilation blocks
// so that the other parts may still be used if LCIO is not available.
#if USE_LCIO
#include "IMPL/LCEventImpl.h"
#include "IMPL/TrackerRawDataImpl.h"
#include "IMPL/LCCollectionVec.h"
#include "lcio.h"
#endif

#if USE_EUTELESCOPE
#include "EUTELESCOPE.h"
#include "EUTelSetupDescription.h"
#include "EUTelEventImpl.h"
#include "EUTelTrackerDataInterfacerImpl.h"
#include "EUTelGenericSparsePixel.h"
#include "EUTelRunHeaderImpl.h"
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
      const RawDataEvent * cRawEvent = dynamic_cast<const RawDataEvent* > (&ev);
      return (unsigned) cRawEvent->GetTag("TLU_TRIGGER_ID", -1);
    }

    // Here, the data from the RawDataEvent is extracted into a StandardEvent.
    // The return value indicates whether the conversion was successful.
    // Again, this is just an example, adapted it for the actual data layout.
    virtual bool GetStandardSubEvent(StandardEvent &sev,
                                     const Event &ev) const {

      std::string sensortype = "Ph2Sensor";
      if(const RawDataEvent * cRawEvent = dynamic_cast<const RawDataEvent* > (&ev))
      {
          uint32_t cNBlocks = cRawEvent->NumBlocks();
          for(uint32_t sensor_id = 0; sensor_id < cNBlocks; sensor_id++)
          {
              const eudaq::RawDataEvent::data_t data = cRawEvent->GetBlock(sensor_id);

              StandardPlane plane(sensor_id, EVENT_TYPE, sensortype);
              // Set the number of pixels
              int width = getlittleendian<unsigned short>(&data[WIDTH_OFFSET]);
              int height = getlittleendian<unsigned short>(&data[HEIGHT_OFFSET]);
              plane.SetSizeZS(width, height, data.size()/6);
              // Set the trigger ID
              plane.SetTLUEvent(GetTriggerID(ev));

              //get strip data
              uint32_t value_offset = DATA_OFFSET;
              uint32_t pixel_id = 0;
              while(value_offset < data.size())
              {
                  plane.SetPixel(pixel_id,getlittleendian<unsigned short>(&data[value_offset+0]),getlittleendian<unsigned short>(&data[value_offset+2]),getlittleendian<unsigned short>(&data[value_offset+4]));
                  value_offset += 6;
                  pixel_id++;
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

#if USE_LCIO && USE_EUTELESCOPE
    // This is where the conversion to LCIO is done
    virtual lcio::LCEvent *GetLCIOEvent(const Event * /*ev*/) const {
      return 0;
    }
    virtual bool GetLCIOSubEvent(lcio::LCEvent & lcioEvent, const Event & eudaqEvent) const
    {
      if(eudaqEvent.IsBORE() || eudaqEvent.IsEORE())
      {
            return true;
      }

      //set type of the resulting lcio event
      lcioEvent.parameters().setValue( eutelescope::EUTELESCOPE::EVENTTYPE, eutelescope::kDE );

      //pointer to collection which will store data
      LCCollectionVec * zsDataCollection;

      //it can be already in event or has to be created
      bool zsDataCollectionExists = false;
      try
      {
            zsDataCollection = static_cast< LCCollectionVec* > ( lcioEvent.getCollection( "zsdata_ph2" ) );
            zsDataCollectionExists = true;
      }
      catch( lcio::DataNotAvailableException& e )
      {
            zsDataCollection = new LCCollectionVec( lcio::LCIO::TRACKERDATA );
      }

      //create cell encoders to set sensorID and pixel type
      CellIDEncoder<TrackerDataImpl> zsDataEncoder   ( eutelescope::EUTELESCOPE::ZSDATADEFAULTENCODING, zsDataCollection  );

      //this is an event as we sent from Producer, needs to be converted to concrete type RawDataEvent
      const RawDataEvent& ev_raw = dynamic_cast<const RawDataEvent&>(eudaqEvent);

      std::vector<eutelescope::EUTelSetupDescription*>  setupDescription;


      int chip_id_offset = 30;
      int previousSensorID = ev_raw.GetID(0) + chip_id_offset;

      zsDataEncoder["sensorID"] = previousSensorID;
      zsDataEncoder["sparsePixelType"] = eutelescope::kEUTelGenericSparsePixel;

      //prepare a new TrackerData object for the ZS data
      //it contains all the hits for a particular sensor in one event
      std::unique_ptr<lcio::TrackerDataImpl > zsFrame( new lcio::TrackerDataImpl );
      zsDataEncoder.setCellID( zsFrame.get() );

      for(size_t cSensor = 0; cSensor < ev_raw.NumBlocks(); ++cSensor)
      {
            const std::vector <unsigned char>& buffer=dynamic_cast<const std::vector<unsigned char>&> (ev_raw.GetBlock(cSensor));

            int cSensorID = ev_raw.GetID(cSensor) + chip_id_offset;

            if(previousSensorID != cSensorID)
            {
                    //write TrackerData object that contains info from one sensor to LCIO collection
                    zsDataCollection->push_back( zsFrame.release() );

                    std::unique_ptr<lcio::TrackerDataImpl> newZsFrame( new lcio::TrackerDataImpl);
                    zsFrame = std::move(newZsFrame);

                    zsDataEncoder["sensorID"] = cSensorID;
                    zsDataEncoder.setCellID( zsFrame.get() );

                    previousSensorID = cSensorID;
            }

            //this is the structure that will host the sparse pixel
            //it helps to decode (and later to decode) parameters of all hits (x, y, charge, ...) to
            //a single TrackerData object (zsFrame) that will correspond to a single sensor in one event
            std::unique_ptr< eutelescope::EUTelTrackerDataInterfacerImpl< eutelescope::EUTelGenericSparsePixel > >
            sparseFrame( new eutelescope::EUTelTrackerDataInterfacerImpl< eutelescope::EUTelGenericSparsePixel > ( zsFrame.get() ) );

            uint32_t value_offset = DATA_OFFSET;
            while(value_offset < buffer.size())
            {
                eutelescope::EUTelGenericSparsePixel thisHit( getlittleendian<unsigned short>(&buffer[value_offset+0]),getlittleendian<unsigned short>(&buffer[value_offset+2]),getlittleendian<unsigned short>(&buffer[value_offset+4]), 0);
                sparseFrame->addSparsePixel( &thisHit );
                value_offset += 6;
            }
      }

      zsDataCollection->push_back( zsFrame.release() );

      //add this collection to lcio event
      if( ( !zsDataCollectionExists )  && ( zsDataCollection->size() != 0 ) )
      {
            lcioEvent.addCollection( zsDataCollection, "zsdata_ph2" );
      }

      // set parameters
      LCEventImpl &lcioEventImpl = dynamic_cast<LCEventImpl&>(lcioEvent);
      lcioEventImpl.setDetectorName("Ph2Sensor");
      lcioEventImpl.setTimeStamp(GetTriggerID(ev_raw));
      const std::map<std::string, std::string> cRawTags = ev_raw.GetTags();
      for(auto item : cRawTags) {
        lcioEventImpl.parameters().setValue(item.first,item.second);
      }

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
