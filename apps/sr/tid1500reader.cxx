#include "dcmtk/ofstd/ofstream.h"
#include "dcmtk/ofstd/oftest.h"

#include "dcmtk/dcmsr/dsrdoc.h"
#include "dcmtk/dcmdata/dcfilefo.h"
#include "dcmtk/dcmiod/modhelp.h"

#include "dcmtk/dcmsr/codes/dcm.h"
#include "dcmtk/dcmsr/cmr/tid1500.h"


#include <json/json.h>

#include "Exceptions.h"

#include "Helper.h"

using namespace std;

// not used yet
static OFLogger dcemfinfLogger = OFLog::getLogger("qiicr.apps");

#include "tid1500readerCLP.h"

#define CHECK_BOOL(condition) \
  do { \
    if (!condition) { \
      std::cerr << "Expected True in " __FILE__ << ":" << __LINE__ << " " << std::cout; \
      throw -1; \
    } \
  } while (0);

#define STATIC_ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))

DSRCodedEntryValue json2cev(Json::Value& j){
  return DSRCodedEntryValue(j["codeValue"].asCString(),
                            j["codingSchemeDesignator"].asCString(),
                            j["codeMeaning"].asCString());
}

void addFileToEvidence(DSRDocument &doc, string dirStr, string fileStr){
  DcmFileFormat ff;
  OFString fullPath;
  CHECK_COND(ff.loadFile(OFStandard::combineDirAndFilename(fullPath,dirStr.c_str(),fileStr.c_str())));
  CHECK_COND(doc.getCurrentRequestedProcedureEvidence().addItem(*ff.getDataset()));
}

bool isCompositeEvidence(OFString& sopClassUID) {
  const char* compositeContextSOPClasses[] = {UID_SegmentationStorage, UID_RealWorldValueMappingStorage};
  int length = sizeof(compositeContextSOPClasses)/sizeof(*compositeContextSOPClasses);
  for( unsigned int i=0; i<length; i++)
    if (sopClassUID == compositeContextSOPClasses[i])
      return true;
  return false;
}

Json::Value getMeasurements(DSRDocument &doc) {
  Json::Value measurements(Json::arrayValue);
  DSRDocumentTree &st = doc.getTree();

  DSRDocumentTreeNodeCursor cursor;
  st.getCursorToRootNode(cursor);
  if(st.gotoNamedChildNode(CODE_DCM_ImagingMeasurements)) {
    // can have 1-n MeasurementGroups
    do {
      if (st.gotoNamedChildNode(CODE_DCM_MeasurementGroup)) {
        Json::Value measurement;
        if (st.gotoNamedChildNode(DSRCodedEntryValue("C67447", "NCIt", "Activity Session"))) {
          // TODO: think about it
          cout << "Activity Session: " << st.getCurrentContentItem().getStringValue().c_str() << endl;
          measurement["ActivitySession"] = st.getCurrentContentItem().getStringValue().c_str();
        }
        st.gotoParent();
        if (st.gotoNamedChildNode(CODE_DCM_ReferencedSegment)) {
          DSRImageReferenceValue referenceImage = st.getCurrentContentItem().getImageReference();
          OFVector<Uint16> items;
          referenceImage.getSegmentList().getItems(items);
          cout << "Reference Segment: " << items[0] << endl;
          measurement["ReferencedSegment"] = items[0];
          if (!referenceImage.getSOPInstanceUID().empty()){
            measurement["segmentationSOPInstanceUID"] = referenceImage.getSOPInstanceUID().c_str();
          }
          st.gotoParent();
        }
        st.gotoParent();
        if (st.gotoNamedChildNode(CODE_DCM_SourceSeriesForSegmentation)) {
          cout << "SourceSeriesForImageSegmentation: " << st.getCurrentContentItem().getStringValue().c_str() << endl;
          measurement["SourceSeriesForImageSegmentation"] = st.getCurrentContentItem().getStringValue().c_str();
          st.gotoParent();
        }
        if (st.gotoNamedChildNode(CODE_DCM_TrackingIdentifier)) {
          cout << "TrackingIdentifier: " << st.getCurrentContentItem().getStringValue().c_str() << endl;
          measurement["TrackingIdentifier"] = st.getCurrentContentItem().getStringValue().c_str();
          st.gotoParent();
        }
        if (st.gotoNamedChildNode(CODE_DCM_Finding)) {
          measurement["Finding"]["CodeMeaning"] = st.getCurrentContentItem().getCodeValue().getCodeMeaning().c_str();
          measurement["Finding"]["CodingSchemeDesignator"] = st.getCurrentContentItem().getCodeValue().getCodingSchemeDesignator().c_str();
          measurement["Finding"]["CodeValue"] = st.getCurrentContentItem().getCodeValue().getCodeValue().c_str();
        }
        st.gotoParent();
        if (st.gotoNamedChildNode(DSRCodedEntryValue("G-C0E3", "SRT", "Finding Site"))) {
          measurement["FindingSite"]["CodeMeaning"] = st.getCurrentContentItem().getCodeValue().getCodeMeaning().c_str();
          measurement["FindingSite"]["CodingSchemeDesignator"] = st.getCurrentContentItem().getCodeValue().getCodingSchemeDesignator().c_str();
          measurement["FindingSite"]["CodeValue"] = st.getCurrentContentItem().getCodeValue().getCodeValue().c_str();
        }
        st.gotoParent();
        st.gotoChild();

        Json::Value measurementItems(Json::arrayValue);
        while (st.gotoNext()){
          if (st.getCurrentContentItem().getNumericValuePtr() != NULL) {
            DSRNumericMeasurementValue measurementValue = st.getCurrentContentItem().getNumericValue();

            Json::Value localMeasurement;
            localMeasurement["value"] = measurementValue.getNumericValue().c_str();

            localMeasurement["units"]["CodeValue"] = measurementValue.getMeasurementUnit().getCodeValue().c_str();
            localMeasurement["units"]["CodeMeaning"] = measurementValue.getMeasurementUnit().getCodeMeaning().c_str();
            localMeasurement["units"]["CodingSchemeDesignator"] = measurementValue.getMeasurementUnit().getCodingSchemeDesignator().c_str();

            localMeasurement["quantity"]["CodeValue"] = st.getCurrentContentItem().getConceptName().getCodeValue().c_str();
            localMeasurement["quantity"]["CodeMeaning"] = st.getCurrentContentItem().getConceptName().getCodeMeaning().c_str();
            localMeasurement["quantity"]["CodingSchemeDesignator"] = st.getCurrentContentItem().getConceptName().getCodingSchemeDesignator().c_str();

            if(st.gotoNamedChildNode(CODE_DCM_Derivation)){
              localMeasurement["derivationModifier"]["CodeValue"] = st.getCurrentContentItem().getCodeValue().getCodeValue().c_str();
              localMeasurement["derivationModifier"]["CodeMeaning"] = st.getCurrentContentItem().getCodeValue().getCodeMeaning().c_str();
              localMeasurement["derivationModifier"]["CodingSchemeDesignator"] = st.getCurrentContentItem().getCodeValue().getCodingSchemeDesignator().c_str();
              st.gotoParent();
            }
            measurementItems.append(localMeasurement);
          }
        }
        measurement["measurementItems"] = measurementItems;
        measurements.append(measurement);
      }
    } while (st.gotoNextNamedNode(CODE_DCM_MeasurementGroup));
  }
  return measurements;
}



int main(int argc, char** argv){
  PARSE_ARGS;

  Json::Value metaRoot;

  DcmFileFormat sliceFF;
  CHECK_COND(sliceFF.loadFile(inputSRFileName.c_str()));
  DcmDataset* dataset = sliceFF.getDataset();

  TID1500_MeasurementReport report(CMR_CID7021::ImagingMeasurementReport);
  DSRDocument doc;

  CHECK_COND(doc.read(*dataset));


  OFString temp;
  doc.getSeriesDescription(temp);
  metaRoot["SeriesDescription"] = temp.c_str();
  doc.getSeriesNumber(temp);
  metaRoot["SeriesNumber"] = temp.c_str();
  doc.getInstanceNumber(temp);
  metaRoot["InstanceNumber"] = temp.c_str();

  cout << "Number of verifying observers: " << doc.getNumberOfVerifyingObservers() << endl;

  OFString observerName, observingDateTime, organizationName;
  if (doc.getNumberOfVerifyingObservers() != 0) {
    doc.getVerifyingObserver(1, observingDateTime, observerName, organizationName);
    metaRoot["observerContext"]["ObserverType"] = "PERSON";
    metaRoot["observerContext"]["PersonObserverName"] = observerName.c_str();
  }

  metaRoot["VerificationFlag"] = DSRTypes::verificationFlagToEnumeratedValue(doc.getVerificationFlag());
  metaRoot["CompletionFlag"] = DSRTypes::completionFlagToEnumeratedValue(doc.getCompletionFlag());

  Json::Value compositeContextUIDs(Json::arrayValue);
  Json::Value imageLibraryUIDs(Json::arrayValue);

  // TODO: We need to think about that, because actually the file names are stored in the json and not the UIDs
  DSRSOPInstanceReferenceList &evidenceList = doc.getCurrentRequestedProcedureEvidence();
  OFCondition cond = evidenceList.gotoFirstItem();
  OFString sopInstanceUID;
  OFString sopClassUID;
  while(cond.good()) {
    evidenceList.getSOPClassUID(sopClassUID);
    evidenceList.getSOPInstanceUID(sopInstanceUID).c_str();
    if (isCompositeEvidence(sopClassUID)) {
//      cout << "add composite" << endl;
      compositeContextUIDs.append(sopInstanceUID.c_str());
    }else {
//      cout << "add image library" << endl;
      imageLibraryUIDs.append(sopInstanceUID.c_str());
    }
    cond = evidenceList.gotoNextItem();
  }
  if (!imageLibraryUIDs.empty())
    metaRoot["imageLibrary"] = imageLibraryUIDs;
  if (!compositeContextUIDs.empty())
    metaRoot["compositeContext"] = compositeContextUIDs;

  metaRoot["Measurements"] = getMeasurements(doc);

  ofstream outputFile;

  outputFile.open(metaDataFileName.c_str());

  outputFile << metaRoot;
  outputFile.close();

//  std::filebuf fb;
//  fb.open (metaDataFileName.c_str(),std::ios::out);
//
//  ostream out(&fb);
//  DSRDocumentTree &st = doc.getTree();
//  st.print(out);
//  fb.close();

  return 0;
}