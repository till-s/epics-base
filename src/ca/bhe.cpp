/*************************************************************************\
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
*     National Laboratory.
* Copyright (c) 2002 The Regents of the University of California, as
*     Operator of Los Alamos National Laboratory.
* EPICS BASE Versions 3.13.7
* and higher are distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution. 
\*************************************************************************/
    
/*  $Id$
 *
 *                    L O S  A L A M O S
 *              Los Alamos National Laboratory
 *               Los Alamos, New Mexico 87545
 *
 *  Copyright, 1986, The Regents of the University of California.
 *
 *  Author: Jeff Hill
 */

#include <stdexcept>
#include <limits.h>
#include <float.h>

#define epicsAssertAuthor "Jeff Hill johill@lanl.gov"

#define epicsExportSharedSymbols
#include "iocinf.h"
#include "virtualCircuit.h"
#include "bhe.h"

/*
 * set average to -1.0 so that when the next beacon
 * occurs we can distinguish between:
 * o new server
 * o existing server's beacon we are seeing
 *  for the first time shortly after program
 *  start up
 *
 * if creating this in response to a search reply
 * and not in response to a beacon then 
 * we set the beacon time stamp to
 * zero (so we can correctly compute the period
 * between the 1st and 2nd beacons)
 */
bhe::bhe ( const epicsTime & initialTimeStamp, 
          unsigned initialBeaconNumber, const inetAddrID & addr ) epicsThrows (()) :
    inetAddrID ( addr ), timeStamp ( initialTimeStamp ), averagePeriod ( - DBL_MAX ),
    pIIU ( 0 ), lastBeaconNumber ( initialBeaconNumber )
{
#   ifdef DEBUG
    {
        char name[64];
        addr.name ( name, sizeof ( name ) );
        ::printf ( "created beacon entry for %s\n", name );
    }
#   endif
}

bhe::~bhe ()
{
}

void bhe::beaconAnomalyNotify ()
{
    if ( this->pIIU ) {
        this->pIIU->beaconAnomalyNotify ();
    }
}

/*
 * update beacon period
 *
 * updates beacon period, and looks for beacon anomalies
 */
bool bhe::updatePeriod ( const epicsTime & programBeginTime, 
    const epicsTime & currentTime, ca_uint32_t beaconNumber, 
    unsigned protocolRevision )
{
    //
    // this block is enetered if the beacon was created as a side effect of
    // creating a connection and so we dont yet know the first beacon time 
    // and  sequence number
    //
    if ( this->timeStamp == epicsTime () ) {
        if ( CA_V410 ( protocolRevision ) ) {
            this->lastBeaconNumber = beaconNumber;
        }

        this->beaconAnomalyNotify ();

        /* 
         * this is the 1st beacon seen - the beacon time stamp
         * was not initialized during BHE create because
         * a TCP/IP connection created the beacon.
         * (nothing to do but set the beacon time stamp and return)
         */
        this->timeStamp = currentTime;

        return false;
    }

    // 1) detect beacon duplications due to redundant routes
    // 2) detect lost beacons due to input queue overrun or damage
    if ( CA_V410 ( protocolRevision ) ) {
        unsigned beaconSeqAdvance;
        if ( beaconNumber > this->lastBeaconNumber ) {
            beaconSeqAdvance = beaconNumber - this->lastBeaconNumber;
        }
        else {
            beaconSeqAdvance = ( ca_uint32_max - this->lastBeaconNumber ) + beaconNumber;
        }
        this->lastBeaconNumber = beaconNumber;

        // throw out sequence numbers just prior to, or the same as, the last one received 
        // (this situation is probably caused by a temporary duplicate route )
        if ( beaconSeqAdvance == 0 ||  beaconSeqAdvance > ca_uint32_max - 256 ) {
            return false;
        }

        // throw out sequence numbers that jump forward by only a few numbers 
        // (this situation is probably caused by a duplicate route 
        // or a beacon due to input queue overun)
        if ( beaconSeqAdvance > 1 &&  beaconSeqAdvance < 4 ) {
            return false;
        }
    }

    // compute the beacon period (if we have seen at least two beacons)
    bool netChange = false;
    double currentPeriod = currentTime - this->timeStamp;
    if ( this->averagePeriod < 0.0 ) {
        double totalRunningTime;

        this->beaconAnomalyNotify ();

        /*
         * this is the 2nd beacon seen. We cant tell about
         * the change in period at this point so we just
         * initialize the average period and return.
         */
        this->averagePeriod = currentPeriod;

        /*
         * ignore beacons seen for the first time shortly after
         * init, but do not ignore beacons arriving with a short
         * period because the IOC was rebooted soon after the 
         * client starts up.
         */
        totalRunningTime = this->timeStamp - programBeginTime;
        if ( currentPeriod <= totalRunningTime ) {
            netChange = true;
        }
    }
    else {

        /*
         * Is this an IOC seen because of a restored
         * network segment? 
         *
         * It may be possible to get false triggers here 
         * if the client is busy, but this does not cause
         * problems because the echo response will tell us 
         * that the server is available
         */
        if ( currentPeriod >= this->averagePeriod * 1.25 ) {

            /* 
             * trigger on any missing beacon 
             * if connected to this server
             */    
            this->beaconAnomalyNotify ();

            if ( currentPeriod >= this->averagePeriod * 3.25 ) {
                /* 
                 * trigger on any 3 contiguous missing beacons 
                 * if not connected to this server
                 */
                netChange = true;
            }
        }

        /*
         * Is this an IOC seen because of an IOC reboot
         * (beacon come at a higher rate just after the
         * IOC reboots). Lower tolarance here because we
         * dont have to worry about lost beacons.
         *
         * It may be possible to get false triggers here 
         * if the client is busy, but this does not cause
         * problems because the echo response will tell us 
         * that the server is available
         */
        else if ( currentPeriod <= this->averagePeriod * 0.80 ) {
            this->beaconAnomalyNotify ();
            netChange = true;
        }
        else if ( this->pIIU ) {
            // update state of health for active virtual circuits 
            // if the beacon looks ok
            this->pIIU->beaconArrivalNotify ();
        }
    
        // update a running average period
        this->averagePeriod = currentPeriod * 0.125 + 
            this->averagePeriod * 0.875;
    }

    this->timeStamp = currentTime;

    return netChange;
}

void bhe::show ( unsigned /* level */ ) const
{
    ::printf ( "CA beacon hash entry at %p with average period %f\n", 
        static_cast <const void *> ( this ), this->averagePeriod );
}

double bhe::period () const epicsThrows (())
{
    return this->averagePeriod;
}

epicsTime bhe::updateTime () const
{
    return this->timeStamp;
}

void bhe::registerIIU ( tcpiiu & iiu, const epicsTime & currentTime )
{
    this->pIIU = & iiu;
    this->timeStamp = currentTime;
    this->averagePeriod = - DBL_MAX;
}

void bhe::unregisterIIU ( tcpiiu & iiu )
{
    if ( this->pIIU == & iiu ) {
        this->pIIU = 0;
        this->timeStamp = epicsTime();
        this->averagePeriod = - DBL_MAX;
    }
}

void * bhe::operator new ( size_t ) // X aCC 361
{
    // The HPUX compiler seems to require this even though no code
    // calls it directly
    throw std::logic_error ( "why is the compiler calling private operator new" );
}

void bhe::operator delete ( void * )
{
    // Visual C++ .net appears to require operator delete if
    // placement operator delete is defined? I smell a ms rat
    // because if I declare placement new and delete, but
    // comment out the placement delete definition there are
    // no undefined symbols.
    errlogPrintf ( "%s:%d this compiler is confused about placement delete - memory was probably leaked",
        __FILE__, __LINE__ );
}

void * bheFreeStore::allocate ( size_t size )
{
    return freeList.allocate ( size );
}

void bheFreeStore::release ( void * pCadaver )
{
    freeList.release ( pCadaver );
}

epicsShareFunc bheMemoryManager::~bheMemoryManager () {}


