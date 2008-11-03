/*
 * LEGAL NOTICE
 * This computer software was prepared by Battelle Memorial Institute,
 * hereinafter the Contractor, under Contract No. DE-AC05-76RL0 1830
 * with the Department of Energy (DOE). NEITHER THE GOVERNMENT NOR THE
 * CONTRACTOR MAKES ANY WARRANTY, EXPRESS OR IMPLIED, OR ASSUMES ANY
 * LIABILITY FOR THE USE OF THIS SOFTWARE. This notice including this
 * sentence must appear on any copies of this computer software.
 * 
 * EXPORT CONTROL
 * User agrees that the Software will not be shipped, transferred or
 * exported into any country or used in any manner prohibited by the
 * United States Export Administration Act or any other applicable
 * export laws, restrictions or regulations (collectively the "Export Laws").
 * Export of the Software may require some form of license or other
 * authority from the U.S. Government, and failure to obtain such
 * export control license may result in criminal liability under
 * U.S. laws. In addition, if the Software is identified as export controlled
 * items under the Export Laws, User represents and warrants that User
 * is not a citizen, or otherwise located within, an embargoed nation
 * (including without limitation Iran, Syria, Sudan, Cuba, and North Korea)
 *     and that User is not otherwise prohibited
 * under the Export Laws from receiving the Software.
 * 
 * All rights to use the Software are granted on condition that such
 * rights are forfeited if User fails to comply with the terms of
 * this Agreement.
 * 
 * User agrees to identify, defend and hold harmless BATTELLE,
 * its officers, agents and employees from all liability involving
 * the violation of such Export Laws, either directly or indirectly,
 * by User.
 */

/*!
 * \file CSP_backup_calculator.cpp
 * \ingroup Objects
 * \brief CSPBackupCalculator class source file.
 * \author Josh Lurz
 */

#include "util/base/include/definitions.h"
#include <string>
#include <cassert>
#include <xercesc/dom/DOMNode.hpp>
#include <xercesc/dom/DOMNodeList.hpp>

#include "sectors/include/CSP_backup_calculator.h"
#include "util/base/include/util.h"
#include "util/base/include/xml_helper.h"
#include "sectors/include/sector_utils.h"
#include "marketplace/include/marketplace.h"
#include "containers/include/iinfo.h"

using namespace std;
using namespace xercesc;

/*!
 * \brief Constructor.
 */
CSPBackupCalculator::CSPBackupCalculator()
: mMaxSectorLoadServed( 0.15 ),
  mMaxBackupFraction (  0.40 ), 
  mBackupExponent ( 4.0 ) 
{
}

// Documentation is inherited.
CSPBackupCalculator* CSPBackupCalculator::clone() const {
    return new CSPBackupCalculator( *this );
}

// Documentation is inherited.
bool CSPBackupCalculator::isSameType( const std::string& aType ) const {
    return aType == getXMLNameStatic();
}

// Documentation is inherited.
const string& CSPBackupCalculator::getName() const {
    return getXMLNameStatic();
}

/*! \brief Get the XML node name in static form for comparison when parsing XML.
*
* This public function accesses the private constant string, XML_NAME. This way
* the tag is always consistent for both read-in and output and can be easily
* changed. The "==" operator that is used when parsing, required this second
* function to return static.
* \note A function cannot be static and virtual.
* \author Josh Lurz, James Blackwood
* \return The constant XML_NAME as a static.
*/
const string& CSPBackupCalculator::getXMLNameStatic() {
    const static string XML_NAME = "CSP-backup-calculator";
    return XML_NAME;
}

// Documentation is inherited.
bool CSPBackupCalculator::XMLParse( const xercesc::DOMNode* node ){
    /*! \pre make sure we were passed a valid node. */
    assert( node );

    // get all child nodes.
    DOMNodeList* nodeList = node->getChildNodes();
    const Modeltime* modeltime = scenario->getModeltime();

    // loop through the child nodes.
    for( unsigned int i = 0; i < nodeList->getLength(); i++ ){
        DOMNode* curr = nodeList->item( i );
        string nodeName = XMLHelper<string>::safeTranscode( curr->getNodeName() );

        if( nodeName == "#text" ) {
            continue;
        }
        else if( nodeName == "max-backup-fraction" ){
            mMaxBackupFraction = XMLHelper<double>::getValue( curr );
        }
        else if( nodeName == "backup-exponent" ){
            mBackupExponent = XMLHelper<double>::getValue( curr );
        }
        else {
            ILogger& mainLog = ILogger::getLogger( "main_log" );
            mainLog.setLevel( ILogger::WARNING );
            mainLog << "Unrecognized text string: " << nodeName << " found while parsing " << getXMLNameStatic() << "." << endl;
            return false;
        }
    }
    return true;
}

// Documentation is inherited.
void CSPBackupCalculator::toInputXML( ostream& aOut, Tabs* aTabs ) const {
    XMLWriteOpeningTag( getXMLNameStatic(), aOut, aTabs );
    XMLWriteElement( mMaxBackupFraction, "max-backup-fraction", aOut, aTabs );
    XMLWriteElementCheckDefault( mBackupExponent, "backup-exponent", aOut, aTabs, 4.0 );
    XMLWriteClosingTag( getXMLNameStatic(), aOut, aTabs );
}

// Documentation is inherited.
void CSPBackupCalculator::toDebugXML( const int aPeriod, ostream& aOut, Tabs* aTabs ) const {
    XMLWriteOpeningTag( getXMLNameStatic(), aOut, aTabs );
    XMLWriteElement( mMaxBackupFraction, "max-backup-fraction", aOut, aTabs );
    XMLWriteElement( mNoSunDayBackup, "no-sun-days-backup", aOut, aTabs );
    XMLWriteElement( mBackupFraction, "backup-fraction", aOut, aTabs );
    XMLWriteElement( mBackupExponent, "backup-exponent", aOut, aTabs );
    XMLWriteClosingTag( getXMLNameStatic(), aOut, aTabs );
}

// Documentation is inherited.
void CSPBackupCalculator::initCalc( const IInfo* aTechInfo ) {
   mMaxSectorLoadServed = aTechInfo->getDouble( "max-sector-load-served", true );
   double noSunDays = aTechInfo->getDouble( "no-sun-days", true );
   double randomMaintenanceFraction = aTechInfo->getDouble( "random-maintenance-fraction", true );
   double scheduledMaintenance = aTechInfo->getDouble( "scheduled-maintenance", true );
   
   double justNoSunDayBackup = noSunDays / double( 365 );
   // The base level of backup needed is no-sun days minus time spent in scheduled maintenance that can occur on no-sun days
   mNoSunDayBackup = justNoSunDayBackup 
                     - min( scheduledMaintenance * ( 1 - randomMaintenanceFraction ), justNoSunDayBackup );
}

double CSPBackupCalculator::getMarginalBackupCapacity( const string& aSector,
                                                                 const string& aElectricSector,
                                                                 const string& aResource,
                                                                 const string& aRegion,
                                                                 const double aReserveMargin,
                                                                 const double aAverageGridCapacityFactor,
                                                                 const int aPeriod ) const
{    
    //! Marginal backup calculation is used for marginal cost of backup capacity
    //! and used in the cost of backup for share equations. 
    //! This cost is not needed for CSP since the backup capacity comes as part
    //! of the CSP technology and is contained in the CSP tech cost already. A zero is returned.

    return 0.0;
}

double CSPBackupCalculator::getAverageBackupCapacity( const string& aSector,
                                                                const string& aElectricSector,
                                                                const string& aResource,
                                                                const string& aRegion,
                                                                const double aReserveMargin,
                                                                const double aAverageGridCapacityFactor,
                                                                const int aPeriod ) const
{
    //! Average backup is needed for CSP since it is used to compute the amount
    //! of energy used by the backup technology.
    
    // Preconditions
    assert( !aSector.empty() );
    assert( !aElectricSector.empty() );
    assert( !aResource.empty() );
    assert( !aRegion.empty() );
    assert( aReserveMargin >= 0 );
    assert( aAverageGridCapacityFactor > 0 );

    // Determine the intermittent share of output.
    // Note that this method in CSP differs as it is based on energy share not capacity
    double elecShare = calcIntermittentShare( aSector, aElectricSector, aResource,
                                              aRegion, aReserveMargin, aAverageGridCapacityFactor,
                                              aPeriod );

    // No backup required for zero share.
    if( elecShare < util::getSmallNumber() ){
        return 0;
    }

    // Calculate average backup energy not inclusing the no-sun days
    // based on the approximation by Yabei Zhang

    mBackupFraction = mMaxBackupFraction * pow( elecShare, mBackupExponent );
    
    // Convert to capacity. This needs to be done since Intermittent Subsector
    // assumes it is getting a capacity number and converts back to energy.
    // This will only work in energy units if we assign a capacity factor of 1
    // for the backup capacity factor and read in a one in the intermittent supply
    // sector object for backup capacity factor
    double backupCapacityFactor = 1.0;
    
    // Add no-sun days backup
    mBackupFraction += mNoSunDayBackup;
    
    return SectorUtils::convertEnergyToCapacity( backupCapacityFactor,
                                                 mBackupFraction );
}


/*!
 * \brief Calculate the share of the intermittent resource within the
 *        electricity sector.
 * \details Calculates the share of energy of the intermittent resource within
 *          the electricity sector. This is determined using trial values for
 *          the intermittent sector and electricity sector production.
 * \param aSector The name of the sector which requires backup capacity.
 * \param aElectricSector The name of the electricity sector into which the
 *        sector having a backup amount calculated for will feed.
 * \param aResource The name of the resource the sector consumes.
 * \param aRegion Name of the containing region.
 * \param aReserveMargin Reserve margin for the electricity sector.
 * \param aAverageGridCapacityFactor The average electricity grid capacity
 *        factor.
 * \param aPeriod Model period.
 * \return Share of the intermittent resource within within the electricity
 *         sector.
 */
double CSPBackupCalculator::calcIntermittentShare( const string& aSector,
                                                             const string& aElectricSector,
                                                             const string& aResource,
                                                             const string& aRegion,
                                                             const double aReserveMargin,
                                                             const double aAverageGridCapacityFactor,
                                                             const int aPeriod ) const
{
    //! Note that the CSP backup is based on share of energy, not capacity, 
    //! so capacity factor and conversions to capacity not used.
    
    Marketplace* marketplace = scenario->getMarketplace();
    // Get trial amount of overall regional electricity supplied.
    double elecSupply = marketplace->getStoredSupply( aElectricSector, aRegion, aPeriod );
    
    // Electricity supply must be positive unless it is period 0 where the trial
    // supply market has not been setup. 
    assert( elecSupply >= 0 || aPeriod == 0 );
    
    double elecShare = 0;
    if( elecSupply > util::getVerySmallNumber() && mMaxSectorLoadServed > 0 ) {
        // Get amount of energy produced by the sector.
        double sectorSupply = SectorUtils::getTrialSupply( aRegion, aSector,
                                                           aPeriod );

        // Calculate the share based on ratio of CSP supply over total elec supply times
        // fraction of total that is peak and intermediate. Since these are trial
        // values the share may exceed 1.
        elecShare = min( sectorSupply / ( elecSupply * mMaxSectorLoadServed ), 1.0 );

        // Share of electricity must be between 0 and 1 inclusive.
        assert( elecShare >= 0 && elecShare <= 1 );
    }
    return elecShare;
}

