/*! 
* \file mac_generator_scenario_runner.cpp
* \ingroup CIAM
* \brief MACGeneratorScenarioRunner class source file.
* \author Josh Lurz
* \date $Date$
* \version $Revision$
*/

#include "util/base/include/definitions.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <string>

#include "containers/include/mac_generator_scenario_runner.h"
#include "containers/include/scenario_runner.h"
#include "containers/include/scenario.h"
#include "containers/include/world.h"
#include "util/base/include/util.h"
#include "util/curves/include/curve.h"
#include "util/curves/include/point_set_curve.h"
#include "util/curves/include/point_set.h"
#include "util/curves/include/xy_data_point.h"
#include "util/base/include/configuration.h"
#include "sectors/include/ag_sector.h"
#include "util/base/include/timer.h"
#include "util/base/include/model_time.h"
#include "marketplace/include/marketplace.h"
#include "util/base/include/xml_helper.h"
#include "util/curves/include/explicit_point_set.h"
using namespace std;

// Function Prototypes
extern void createDBout();
extern void openDB();
extern void createMCvarid();
extern void closeDB();

extern ofstream bugoutfile, logfile, outFile;

/*! \brief Constructor
* \param scenarioIn A pointer to the scenario which will be run.
*/
MACGeneratorScenarioRunner::MACGeneratorScenarioRunner( Scenario* scenarioIn )
:ScenarioRunner( scenarioIn ){
}

//! Destructor
MACGeneratorScenarioRunner::~MACGeneratorScenarioRunner(){
}

/*! \brief Function which handles running the scenario and optionally
* computing a cost curve.
* \details This function wraps around the scenario so that scenario can be called
* multiple times if neccessary to create an abatement cost curve. This function first calls
* the scenario regularly, outputs all data, and then determines whether to create the cost curve.
* A helper function performs those calculations if neccessary.
* \author Josh Lurz
*/
void MACGeneratorScenarioRunner::runScenario( Timer& timer ) {
    
    // Perform the initial run of the scenario.
    scenario->run();
    
    // Compute model run time.
    timer.save();
    timer.print( cout, "Data Readin & Initial Model Run Time:" );
    
    // Print the output for the initial run.
    printOutput();
    
    // Print the timestamps.
    timer.save();
    timer.print( logfile, "Data Readin, Model Run & Write Time:" );
    
    const Configuration* conf = Configuration::getInstance();
    
    if ( conf->getBool( "timestamp" ) ) { 
        timer.print( bugoutfile, "Data Readin, Model Run & Write Time:" );
    }

    // Create cost curve if configured.
    if( conf->getBool( "createCostCurve", false ) ){
        calculateAbatementCostCurve();
    }

}

/*! \brief Function to create a cost curve for the mitigation policy.
* \details This function performs multiple calls to scenario.run() with 
* varied fixed carbon taxes in order to determine an abatement cost curve.
* \note This code could be readily modified to deal with other gasses at this level. 
* \todo Break this function up into several smaller more intelligent ones. Its too long. -JPL
*/
void MACGeneratorScenarioRunner::calculateAbatementCostCurve() {
    // Determine the number of additional points to calculate. 
    const Configuration* conf = Configuration::getInstance();
    const int numPoints = conf->getInt( "numPointsForCO2CostCurve", 5 );
    const Modeltime* modeltime = scenario->getModeltime();
    const int maxPeriod = modeltime->getmaxper();
    
    // Get the name of an output file.
    const string ccOutputFileName = conf->getFile( "costCurvesOutputFileName", "cost_curves.xml" );

    // Open the output file.
    ofstream ccOut;
    ccOut.open( ccOutputFileName.c_str(), ios::out );
    util::checkIsOpen( ccOut, ccOutputFileName );
    
    // Create a root tag.
    Tabs tabs;
	XMLWriteOpeningTag( "CostCurvesInfo", ccOut, &tabs ); 

    typedef map<const string, const Curve*>::const_iterator RegionIter;
    // Create vectors of emissions quantities and costs.
    vector<map<const string, const Curve*> > emissionsQCurves( numPoints + 1 );
    vector<map<const string, const Curve*> > emissionsTCurves( numPoints + 1 );
    
    // Temporarily hard coded string.
    const string GHG_NAME = "CO2";

    // Get prices and emissions for the primary scenario run.
    emissionsQCurves[ numPoints ] = scenario->getEmissionsQuantityCurves( GHG_NAME );
    emissionsTCurves[ numPoints ] = scenario->getEmissionsPriceCurves( GHG_NAME );
    
    // TEMP
    XMLWriteOpeningTag( "USARegionCurves", ccOut, &tabs );
    // Run a trial for each point. 
    
    World* world = scenario->getWorld();
    for( int currPoint = 0; currPoint < numPoints; currPoint++ ){
        // Determine the fraction of the full tax this tax will be.
        const double fraction = static_cast<double>( currPoint ) / static_cast<double>( numPoints );
        
        // Iterate through the regions to set different taxes for each if neccessary.
        // Currently this will set the same for all of them.
        for( RegionIter rIter = emissionsTCurves[ numPoints ].begin(); rIter != emissionsTCurves[ numPoints ].end(); rIter++ ){
            // Vector which will contain taxes for this trial.
            vector<double> currTaxes( maxPeriod );
        
            // Set the tax for each year. 
            for( int per = 0; per < maxPeriod; per++ ){
                const int year = modeltime->getper_to_yr( per );
                currTaxes[ per ] = rIter->second->getY( year ) * fraction;
            }
        
            // Set the fixed taxes into the world.
            world->setFixedTaxes( GHG_NAME, rIter->first, currTaxes );
        }

        cout << endl << "Running new trial...." << endl << endl;

        // Create an ending for the output files using the run number.
        stringstream outputEnding;
        outputEnding << currPoint;
        string filenameEnding;
        outputEnding >> filenameEnding;
        scenario->run( filenameEnding );
      
        // Save information.
        emissionsQCurves[ currPoint ] = scenario->getEmissionsQuantityCurves( GHG_NAME );
        emissionsTCurves[ currPoint ] = scenario->getEmissionsPriceCurves( GHG_NAME );
    }   
    
    // Create curves for each period based on all trials.
    vector<map<const string, Curve*> > periodCostCurves( maxPeriod );
    
    XMLWriteOpeningTag( "PeriodCostCurves", ccOut, &tabs );
    for( int per = 0; per < maxPeriod; per++ ){
        const int year = modeltime->getper_to_yr( per );
        XMLWriteOpeningTag( "CostCurves", ccOut, &tabs, "", per );
        // Iterate over each region.
        for( RegionIter rIter = emissionsQCurves[ 0 ].begin(); rIter != emissionsQCurves[ 0 ].end(); rIter++ ){
            ExplicitPointSet* currPoints = new ExplicitPointSet();
            const string region = rIter->first;
            // Iterate over each trial.
            for( int trial = 0; trial < numPoints + 1; trial++ ){
                const double reduction = rIter->second->getY( year ) - emissionsQCurves[ trial ][ region ]->getY( year );
                const double tax = emissionsTCurves[ trial ][ region ]->getY( year );
                XYDataPoint* currPoint = new XYDataPoint( reduction, tax );
                currPoints->addPoint( currPoint );
            }
            Curve* perCostCurve = new PointSetCurve( currPoints );
            perCostCurve->setTitle( region + " period cost curve" );
            perCostCurve->setNumericalLabel( per );
            
            // Only write out USA data for now. 
            if( region == "USA" ){
                perCostCurve->toInputXML( ccOut, &tabs );
            }
            periodCostCurves[ per ][ region ] = perCostCurve;
        }
        XMLWriteClosingTag( "CostCurves", ccOut, &tabs );
       
    }
    XMLWriteClosingTag( "PeriodCostCurves", ccOut, &tabs );
    // Storage for the final cost curves. 
    map<const string, const Curve*> regionalCostCurves;
    
    // Iterate through the regions again to determine the cost per period.
    const vector<string> regions = scenario->getWorld()->getRegionVector();
    typedef vector<string>::const_iterator RNameIter;
    double globalCost = 0;
    double globalDiscountedCost = 0;
    map<const string, double> regionalCosts;
    map<const string, double> regionalDiscountedCosts;

	XMLWriteOpeningTag( "RegionalCostCurvesByPeriod", ccOut, &tabs );
    const double discountRate = conf->getDouble( "discountRate", 0.05 );

    for( RNameIter rNameIter = regions.begin(); rNameIter != regions.end(); rNameIter++ ){
        ExplicitPointSet* costPoints = new ExplicitPointSet();

        // Loop through the periods. 
        for( int per = 0; per < maxPeriod; per++ ){
            const int year = modeltime->getper_to_yr( per );
            double periodCost = periodCostCurves[ per ][ *rNameIter ]->getIntegral( 0, DBL_MAX ); // Integrate from zero to the reduction.
            XYDataPoint* currPoint = new XYDataPoint( year, periodCost );
            costPoints->addPoint( currPoint );
        }
        Curve* regCostCurve = new PointSetCurve( costPoints );
        regCostCurve->setTitle( *rNameIter );
        regCostCurve->toInputXML( ccOut, &tabs );
        const double regionalCost = regCostCurve->getIntegral( modeltime->getper_to_yr( 1 ), modeltime->getendyr() );

        // Temporary hardcoding of start year.
        const double discountedRegionalCost = regCostCurve->getDiscountedValue( modeltime->getper_to_yr( 1 ), modeltime->getendyr(), discountRate );
        regionalCostCurves[ *rNameIter ] = regCostCurve;
        regionalCosts[ *rNameIter ] = regionalCost;
        regionalDiscountedCosts[ *rNameIter ] = discountedRegionalCost;

        // Check if we are double summing globalcost!
        globalCost += regionalCost;
        globalDiscountedCost += discountedRegionalCost;
    }

	XMLWriteClosingTag( "RegionalCostCurvesByPeriod", ccOut, &tabs ); 
    /*! \todo discounting and output */

    // Write out undiscounted costs by region.
	XMLWriteOpeningTag( "RegionalUndiscountedCosts", ccOut, &tabs );
    typedef map<const string,double>::const_iterator constDoubleMapIter;
    for( constDoubleMapIter iter = regionalCosts.begin(); iter != regionalCosts.end(); iter++ ){
        XMLWriteElement( iter->second, "UndiscountedCost", ccOut, &tabs, 0, iter->first );
    }
	XMLWriteClosingTag( "RegionalUndiscountedCosts", ccOut, &tabs );
    // End of writing undiscounted costs by region.
     
    // Write out discounted costs by region.
	XMLWriteOpeningTag( "RegionalDiscountedCosts", ccOut, &tabs );
    typedef map<const string,double>::const_iterator constDoubleMapIter;
    for( constDoubleMapIter iter = regionalDiscountedCosts.begin(); iter != regionalDiscountedCosts.end(); iter++ ){
        XMLWriteElement( iter->second, "DiscountedCost", ccOut, &tabs, 0, iter->first );
    }
	XMLWriteClosingTag( "RegionalDiscountedCosts", ccOut, &tabs );
    // End of writing undiscounted costs by region.

    // Write out the total cost.
    XMLWriteElement( globalCost, "GlobalUndiscountedTotalCost", ccOut, &tabs );
    XMLWriteElement( globalDiscountedCost, "GlobalDiscountedCost", ccOut, &tabs );

	XMLWriteClosingTag( "CostCurvesInfo", ccOut, &tabs );
    ccOut.close();

    // Clean up memory.
    for( int i = 0; i < numPoints + 1; i++ ){
        for( RNameIter rNameIter = regions.begin(); rNameIter != regions.end(); rNameIter++ ){
            delete emissionsQCurves[ i ][ *rNameIter ];
            delete emissionsTCurves[ i ][ *rNameIter ];
        }
    }

    for( int i = 0; i < maxPeriod; i++ ){
        for( RNameIter rNameIter = regions.begin(); rNameIter != regions.end(); rNameIter++ ){
            delete periodCostCurves[ i ][ *rNameIter ];
        }
    }
    for( RNameIter rNameIter = regions.begin(); rNameIter != regions.end(); rNameIter++ ){
        delete regionalCostCurves[ *rNameIter ];
    }
    
}

/*! \brief Function to perform both file and database output. 
* \details This function write out the XML, file and database output.
* All file names are defined by the configuration file. All file handles
* are closed when the function completes.
*/
void MACGeneratorScenarioRunner::printOutput() const {
    
    // Get a pointer to the configuration object.
    const Configuration* conf = Configuration::getInstance();
        
    // Print output xml file.
    const string xmlOutFileName = conf->getFile( "xmlOutputFileName", "output.xml" );
    ofstream xmlOut;
    xmlOut.open( xmlOutFileName.c_str(), ios::out );
    util::checkIsOpen( xmlOut, xmlOutFileName );
    
    Tabs* tabs = new Tabs();
    scenario->toInputXML( xmlOut, tabs );
    delete tabs;

    // Close the output file. 
    xmlOut.close();
    
   
    // Open the output file.
    const string outFileName = conf->getFile( "outFileName" );
    outFile.open( outFileName.c_str(), ios::out );
    util::checkIsOpen( outFile, outFileName ); 
    
    // Write results to the output file.
    // Minicam style output.
    outFile << "Region,Sector,Subsector,Technology,Variable,Units,";
    
    for ( int t = 0; t < scenario->getModeltime()->getmaxper(); t++ ) { 
        outFile << scenario->getModeltime()->getper_to_yr( t ) <<",";
    }
    outFile << "Date,Notes" << endl;
    scenario->getWorld()->csvOutputFile();
    outFile.close();
    
    // Write out the ag sector data.
    if( conf->getBool( "agSectorActive" ) ){
        AgSector::internalOutput();
    }
    
    // Perform the database output. 
    openDB(); // Open MS Access database
    createDBout(); // create main database output table before calling output routines
    scenario->getWorld()->dbOutput(); // MiniCAM style output to database
    scenario->getMarketplace()->dbOutput(); // write global market info to database
    createMCvarid(); // create MC variable id's     
    
    // close MS Access database
    closeDB();
}