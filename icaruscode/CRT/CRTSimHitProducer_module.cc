/////////////////////////////////////////////////////////////////////////////
///// Class:       CRTSimHitProducer
///// Module Type: producer
///// File:        CRTSimHitProducer_module.cc
/////
///// Author:         Thomas Brooks
///// E-mail address: tbrooks@fnal.gov
/////
///// Modified from CRTSimHitProducer by Thomas Warburton.
///////////////////////////////////////////////////////////////////////////////

//// icaruscode includes
#include "icaruscode/CRT/CRTProducts/CRTChannelData.h"
#include "icaruscode/CRT/CRTProducts/CRTData.hh"
#include "icaruscode/CRT/CRTProducts/CRTHit.h"

// Framework includes
#include "art/Framework/Core/EDProducer.h"
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Principal/Handle.h"
#include "art/Framework/Principal/Event.h" 
#include "canvas/Persistency/Common/Ptr.h" 
#include "canvas/Persistency/Common/PtrVector.h" 
#include "art/Framework/Principal/Run.h"
#include "art/Framework/Principal/SubRun.h"
#include "art/Framework/Services/Optional/TFileService.h" 
#include "art/Framework/Services/Optional/TFileDirectory.h"
#include "larcorealg/CoreUtils/NumericUtils.h"
#include "canvas/Utilities/InputTag.h"
#include "fhiclcpp/ParameterSet.h"
#include "messagefacility/MessageLogger/MessageLogger.h"

// C++ includes
#include <memory>
#include <iostream>
#include <map>
#include <iterator>
#include <algorithm>

// LArSoft
#include "lardataobj/Simulation/SimChannel.h"
#include "lardataobj/Simulation/AuxDetSimChannel.h"
#include "larcore/Geometry/Geometry.h"
#include "larcore/Geometry/AuxDetGeometry.h"
#include "larcorealg/Geometry/GeometryCore.h"
#include "larcorealg/Geometry/PlaneGeo.h"
#include "larcorealg/Geometry/WireGeo.h"
#include "lardataobj/RecoBase/OpFlash.h"
#include "lardata/Utilities/AssociationUtil.h"
#include "lardata/DetectorInfoServices/LArPropertiesService.h"
#include "lardata/DetectorInfoServices/DetectorPropertiesService.h"
#include "lardata/DetectorInfoServices/DetectorClocksService.h"
#include "lardataobj/RawData/ExternalTrigger.h"
#include "larcoreobj/SimpleTypesAndConstants/PhysicalConstants.h"

// ROOT
#include "TTree.h"
#include "TFile.h"
#include "TH1D.h"
#include "TH2D.h"
#include "TVector3.h"

namespace {
  // Local namespace for local functions

  char MacToType(uint32_t mac);
  uint32_t MacToRegion(uint32_t mac);
  uint32_t MacToAuxDetID(uint32_t mac, uint32_t chan);
  uint32_t ChannelToAuxDetSensitiveID(uint32_t mac, uint32_t chan);

} //namespace local

namespace icarus {
namespace crt {
  
  class CRTSimHitProducer : public art::EDProducer {
  public:

    explicit CRTSimHitProducer(fhicl::ParameterSet const & p);
    // The destructor generated by the compiler is fine for classes
    // without bare pointers or other resource use.

    // Plugins should not be copied or assigned.
    CRTSimHitProducer(CRTSimHitProducer const &) = delete;
    CRTSimHitProducer(CRTSimHitProducer &&) = delete;
    CRTSimHitProducer & operator = (CRTSimHitProducer const &) = delete; 
    CRTSimHitProducer & operator = (CRTSimHitProducer &&) = delete;

    // Required functions.
    void produce(art::Event & e) override;

    // Selected optional functions.
    void beginJob() override;

    void endJob() override;

    void reconfigure(fhicl::ParameterSet const & p);

  private:

    // Params from fcl file.......
    art::InputTag fCrtModuleLabel;      ///< name of crt producer
    bool          fVerbose;             ///< print info
    double        fQPed;                ///< Pedestal offset of SiPMs [ADC]
    double        fQSlope;              ///< Pedestal slope of SiPMs [ADC/photon]
    bool          fUseReadoutWindow;    ///< Only reconstruct hits within readout window

    // Other variables shared between different methods.
    geo::GeometryCore const* fGeometryService;                 ///< pointer to Geometry provider
    detinfo::DetectorProperties const* fDetectorProperties;    ///< pointer to detector properties provider
    //art::ServiceHandle<geo::AuxDetGeometry> fAuxDetGeoService;
    //const geo::AuxDetGeometry* fAuxDetGeo;
    //const geo::AuxDetGeometryCore* fAuxDetGeoCore;

  }; // class CRTSimHitProducer 

  CRTSimHitProducer::CRTSimHitProducer(fhicl::ParameterSet const & p)
  // Initialize member data here, if know don't want to reconfigure on the fly
  {
    // Call appropriate produces<>() functions here.
    produces< std::vector<icarus::crt::CRTHit> >();
    
    // Get a pointer to the geometry service provider
    fGeometryService = lar::providerFrom<geo::Geometry>();
    fDetectorProperties = lar::providerFrom<detinfo::DetectorPropertiesService>(); 

    reconfigure(p);

  } // CRTSimHitProducer()

  void CRTSimHitProducer::reconfigure(fhicl::ParameterSet const & p)
  {
    fCrtModuleLabel       = (p.get<art::InputTag> ("CrtModuleLabel")); 
    fVerbose              = (p.get<bool> ("Verbose"));
    fQPed                 = (p.get<double> ("QPed"));
    fQSlope               = (p.get<double> ("QSlope"));
    fUseReadoutWindow     = (p.get<bool> ("UseReadoutWindow"));
  }

  void CRTSimHitProducer::beginJob()
  {
    if(fVerbose){std::cout<<"----------------- CRT Hit Reco Module -------------------"<<std::endl;}
  } // beginJob()

  void CRTSimHitProducer::produce(art::Event & event)
  {

    if(fVerbose){
      std::cout<<"============================================"<<std::endl
               <<"Run = "<<event.run()<<", SubRun = "<<event.subRun()<<", Event = "<<event.id().event()<<std::endl
               <<"============================================"<<std::endl;
    }

    // Detector properties
    //double readoutWindow  = (double)fDetectorProperties->ReadOutWindowSize();
    //double driftTimeTicks = 2.0*(2.*fGeometryService->DetHalfWidth()+3.)/fDetectorProperties->DriftVelocity();

    // Retrieve list of CRT hits
    art::Handle< std::vector<crt::CRTData>> crtListHandle;
    event.getByLabel(fCrtModuleLabel, crtListHandle);

    // Create anab::T0 objects and make association with recob::Track
    std::unique_ptr< std::vector<icarus::crt::CRTHit> > CRTHits ( new std::vector<icarus::crt::CRTHit>);

    uint16_t nHitMiss = 0, nHitC = 0, nHitD = 0, nHitM = 0;
    if (fVerbose) mf::LogInfo("CRT") << "Found " << crtListHandle->size() << " FEB events" << '\n';

    double origin[3] = {0,0,0}; 
    std::map<uint32_t,uint32_t> regCounts;
    std::set<uint32_t> regs;

    for (auto const &febdat : (*crtListHandle)) {

      uint32_t mac = febdat.Mac5();
      char type = MacToType(mac);
      uint32_t region = MacToRegion(mac);
      std::pair<uint32_t,uint32_t> trigpair = febdat.TrigPair();
      std::pair<uint32_t,uint32_t> macPair = febdat.MacPair();

      if ((regs.insert(region)).second) regCounts[region] = 1;
      else regCounts[region]++;

      uint32_t adid  = MacToAuxDetID(mac,trigpair.first); //module ID
      auto const& adGeo = fGeometryService->AuxDet(adid); //module
      uint32_t adsid1 = ChannelToAuxDetSensitiveID(mac,trigpair.first); //trigger strip ID
      auto const& adsGeo1 = adGeo.SensitiveVolume(adsid1); //trigger strip

      double stripPosWorld1[3], stripPosWorld2[3];
      double modPos1[3], modPos2[3], hitLocal[3];

      adsGeo1.LocalToWorld(origin,stripPosWorld1);
      adGeo.WorldToLocal(stripPosWorld1,modPos1);

      float length = adsGeo1.Length(); //strip length
      float width = adsGeo1.HalfWidth1()*2; //strip width

      float t01=0, t02=0;//, t01corr=0, t02corr=0;
      float t11=0, t12=0;//, t11corr=0, t12corr=0;
      float t0=0, t0corr=0, t1=0, t1corr=0;

      double hitPoint[3];
      double hitPointErr[3];

      //CERN and DC modules have intermodule coincidence
      if ( type == 'c' || type == 'd' ) {
          //time and charge information for each channel above threshold
          std::vector<icarus::crt::CRTChannelData> chandat = febdat.ChanData();
          hitLocal[1] = 0.0;

          //2nd strip within the module that provided the coincidence
          uint32_t  adsid2 = ChannelToAuxDetSensitiveID(mac,trigpair.second);
          auto const& adsGeo2 = adGeo.SensitiveVolume(adsid2);

          adsGeo2.LocalToWorld(origin,stripPosWorld2); //fetch origin of strip in global coords
          adGeo.WorldToLocal(stripPosWorld2,modPos2); //coincidence strip pos in module coords

          //CERN modules have X-Y configuration
          if ( type == 'c' ) {     
   
              if ((adsid1<8&&adsid2<8)||(adsid1>7&&adsid2>7))
                  mf::LogInfo("CRT") << "incorrect CERN trig pair!!" << '\n';
  
              if (adsid1 < 8 ) {
                  hitLocal[0] = modPos1[0];
                  hitLocal[2] = modPos2[2];
              }
              else {
                  hitLocal[0] = modPos2[0];
                  hitLocal[2] = modPos1[2];
              }

              adGeo.LocalToWorld(hitLocal,hitPoint); //tranform from module to world coords

              hitPointErr[0] = width/sqrt(12);
              hitPointErr[1] = adGeo.HalfHeight();
              hitPointErr[2] = width/sqrt(12);
              nHitC++;

          }//if c type

          if ( type == 'd' ) {

             if ((adsid1<32&&adsid2<32)||(adsid1>31&&adsid2>31))
                  mf::LogInfo("CRT") << "incorrect DC trig pair!!" << '\n';

             hitPoint[0] = 0.5 * ( stripPosWorld1[0] + stripPosWorld2[0] );
             hitPoint[1] = 0.5 * ( stripPosWorld1[1] + stripPosWorld2[1] );
             hitPoint[2] = 0.5 * ( stripPosWorld1[2] + stripPosWorld2[2] );

             hitPointErr[0] = abs(stripPosWorld1[0] - stripPosWorld2[0])/sqrt(12);
             hitPointErr[1] = adGeo.HalfHeight();
             hitPointErr[2] = length/sqrt(12);
             nHitD++;
          } // if d type


          for(auto chan : chandat) {

              if (chan.Channel() == trigpair.first) {
                  t01 = chan.T0();
                  t11 = chan.T1();
                  //t01corr = t01;
                  //t11corr = t11;
              }
              if (chan.Channel() == trigpair.second) {
                  t02 = chan.T0();
                  t12 = chan.T1();
                  //t02corr = t02;
                  //t12corr = t12;
              }
              if (t01!=0 && t02!=0) {
                  t0 = 0.5 * ( t01 + t02 );
                  t1 = 0.5 * ( t11 + t12 );
                  t0corr = t0;
                  t1corr = t1;
                  break;
              }
          }//for ChannelData

          if(fVerbose) mf::LogInfo("CRT") << " CRT HIT PRODUCED!" << '\n'
                  << "   AuxDetID: " << adid << '\n'
                  << "   AuxDetSID1 / AuxDetSID2 : " << adsid1 << " / " << adsid2 << '\n'
                  << "   x: " << hitPoint[0] << " , y: " << hitPoint[1] << " , z: " << hitPoint[2] << '\n'
                  << "   xerr: " << hitPointErr[0] << " , yerr: " << hitPointErr[1] << " , zerr: " << hitPointErr[2] << '\n'
                  << "   t0: " << t0 << " , t0corr: " << t0corr << " , t1: " << t1 << " , t1corr: " << t1corr << '\n';

          CRTHits->push_back(icarus::crt::CRTHit(hitPoint[0],hitPoint[1],hitPoint[2],  \
                         hitPointErr[0],hitPointErr[1],hitPointErr[2],  \
                         t0, t0corr, t1, t1corr, macPair,region) ); 
      }//if c or d type

      if ( type == 'm' ) {

          double ttrig1 = febdat.TTrig();
          bool coinModFound = false;

          for (auto const &febdat2 : (*crtListHandle)) {

              uint32_t mac2 = febdat2.Mac5();
              if (febdat.MacPair().second != mac2) continue;
              if (MacToRegion(mac2) != region) continue;

              uint32_t adid2  = MacToAuxDetID(mac2,trigpair.first);
              auto const& adGeo2 = fGeometryService->AuxDet(adid2);
              uint32_t  adsid2 = ChannelToAuxDetSensitiveID(mac2,trigpair.first);
              auto const& adsGeo2 = adGeo2.SensitiveVolume(adsid2);
              adsGeo2.LocalToWorld(origin,stripPosWorld2);

              double ttrig2 = febdat2.TTrig();
              if (util::absDiff(ttrig2,ttrig1)<50) {

                    //if left or right regions (X-X)
                    if (region == 50 || region == 54) {
                        hitPoint[0] = 0.5 * ( stripPosWorld1[0] + stripPosWorld2[0] );
                        hitPoint[1] = 0.5 * ( stripPosWorld1[1] + stripPosWorld2[1] );
                        hitPoint[2] = 0.5 * ( stripPosWorld1[2] + stripPosWorld2[2] );

                        hitPointErr[0] = abs(stripPosWorld1[0] - stripPosWorld2[0])/sqrt(12);
                        hitPointErr[1] = abs(stripPosWorld1[1] - stripPosWorld2[1])/sqrt(12);
                        hitPointErr[2] = length/sqrt(12);

                        t0 = 0.5*(ttrig1+ttrig2);
                        t1 = t0;
                        t0corr = t0;
                        t1corr = t1;

                        coinModFound = true;
                    }// if left or right

                    //if front or back regions (X-Y)
                    if (region == 44 || region == 42) {
                        if ((adid>107&&adid<119&&adid2>107&&adid2<119)||
                            (adid>118&&adid<128&&adid2>118&&adid2<128) )
                            mf::LogInfo("CRT") << "incorrect MINOS mac pair!!" << '\n';

                        if (adid>107&&adid<119) { //if X module
                            hitPoint[1] = stripPosWorld1[1];
                            hitPoint[0] = stripPosWorld2[0];
                            hitPoint[2] = 0.5 * ( stripPosWorld1[2] + stripPosWorld2[2] );
                        }
                        else {
                            hitPoint[0] = stripPosWorld1[1];
                            hitPoint[1] = stripPosWorld2[0];
                            hitPoint[2] = 0.5 * ( stripPosWorld1[2] + stripPosWorld2[2] );
                        }

                        hitPointErr[0] = width/sqrt(12);
                        hitPointErr[1] = width/sqrt(12);
                        hitPointErr[2] = 0.5*util::absDiff( stripPosWorld1[2],stripPosWorld2[2]);

                        t0 = 0.5*(ttrig1+ttrig2);
                        t1 = t0;
                        t0corr = t0;
                        t1corr = t1;

                        coinModFound = true;
                    }//if front or back

              }//if trigger pair module found

              if (coinModFound) {
                  nHitM++;
                  CRTHits->push_back(icarus::crt::CRTHit(hitPoint[0],hitPoint[1],hitPoint[2],  \
                       hitPointErr[0],hitPointErr[1],hitPointErr[2],  \
                       t0, t0corr, t1, t1corr, febdat.MacPair(),region));
                  break;
              }

          }// for febdat         

          if (!coinModFound) nHitMiss++;

      } //if m type

    }//for FEBData

    if(fVerbose) {
          mf::LogInfo("CRT") << CRTHits->size() << " CRT hits produced!" << '\n'
              << "  nHitC: " << nHitC << " , nHitD: " << nHitD << " , nHitM: " << nHitM << '\n'
              << "    " << nHitMiss << " CRT hits missed!" << '\n';
          std::map<uint32_t,uint32_t>::iterator cts = regCounts.begin();
          mf::LogInfo("CRT") << " CRT Hits by region" << '\n';
          while (cts != regCounts.end()) {
              mf::LogInfo("CRT") << "reg: " << (*cts).first << " , hits: " << (*cts).second << '\n';
              cts++;
          }
    }//if Verbose

    event.put(std::move(CRTHits));

  } //produce

  void CRTSimHitProducer::endJob()
  {

  }

  DEFINE_ART_MODULE(CRTSimHitProducer)

} //namespace crt
} //namespace icarus

namespace {
  // Local namespace for local functions

  char MacToType(uint32_t mac) {

      if(mac<=99) return 'm';
      if(mac>=148 && mac<=161) return 'd';
      if(mac>=162&&mac<=283) return 'c';

      return 'e';
  }

  uint32_t MacToRegion(uint32_t mac){

      if(mac>=162 && mac<=245) return 38; //top
      if(mac>=246 && mac<=258) return 52; //slope left
      if(mac>=259 && mac<=271) return 56; //slope right
      if(mac>=272 && mac<=277) return 48; //slope front
      if(mac>=278 && mac<=283) return 46; //slope back
      if(            mac<=17 ) return 50; //left
      if(mac>=50  && mac<=67 ) return 50; //left
      if(mac>=18  && mac<=35 ) return 54; //right
      if(mac>=68  && mac<=85 ) return 54; //right
      if(mac>=36  && mac<=42 ) return 44; //front
      if(mac>=86  && mac<=92 ) return 44; //front
      if(mac>=43  && mac<=49 ) return 42; //back
      if(mac>=93  && mac<=99 ) return 42; //back
      if(mac>=148 && mac<=161) return 58; //bottom

      return 0;
  }

  uint32_t MacToAuxDetID(uint32_t mac, uint32_t chan){
    char type = MacToType(mac);
    if (type == 'e') return UINT32_MAX;
    if (type == 'c' || type == 'd') return mac;
    if (type == 'm') {
      if (            chan<=9 ) return (mac/3)*3;
      if (chan>=10 && chan<=19) return (mac/3)*3 + 1;
      if (chan>=20 && chan<=29) return (mac/3)*3 + 2;
    }

    return UINT32_MAX;
  }

  uint32_t ChannelToAuxDetSensitiveID(uint32_t mac, uint32_t chan) {
    char type = MacToType(mac);
    if (type=='d') return chan;
    if (type=='c') return chan/2;
    if (type=='m') return (chan % 10)*2;

    return UINT32_MAX;
  }

} //namespace local
