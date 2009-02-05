/*
 * Copyright (c) 1998-2000 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * The contents of this file constitute Original Code as defined in and
 * are subject to the Apple Public Source License Version 1.1 (the
 * "License").  You may not use this file except in compliance with the
 * License.  Please obtain a copy of the License at
 * http://www.apple.com/publicsource and read it before using this file.
 *
 * This Original Code and all software distributed under the License are
 * distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License.
 *
 * @APPLE_LICENSE_HEADER_END@
 */

#include <IOKit/IOMessage.h>

#include <IOKit/sbp2/IOFireWireSBP2Login.h>
#include <IOKit/sbp2/IOFireWireSBP2LUN.h>
#include <IOKit/sbp2/IOFireWireSBP2Target.h>

#include <IOKit/firewire/IOFireWireController.h>
#include <IOKit/firewire/IOConfigDirectory.h>

#include "FWDebugging.h"
#include "IOFireWireSBP2Diagnostics.h"

OSDefineMetaClassAndStructors( IOFireWireSBP2Login, OSObject );

OSMetaClassDefineReservedUnused(IOFireWireSBP2Login, 0);
OSMetaClassDefineReservedUnused(IOFireWireSBP2Login, 1);
OSMetaClassDefineReservedUnused(IOFireWireSBP2Login, 2);
OSMetaClassDefineReservedUnused(IOFireWireSBP2Login, 3);
OSMetaClassDefineReservedUnused(IOFireWireSBP2Login, 4);
OSMetaClassDefineReservedUnused(IOFireWireSBP2Login, 5);
OSMetaClassDefineReservedUnused(IOFireWireSBP2Login, 6);
OSMetaClassDefineReservedUnused(IOFireWireSBP2Login, 7);
OSMetaClassDefineReservedUnused(IOFireWireSBP2Login, 8);

// initWithLUN
//
// initializer

bool IOFireWireSBP2Login::initWithLUN( IOFireWireSBP2LUN * lun )
{
	IOReturn status = kIOReturnSuccess;
    
    // store LUN & Unit
    fLUN 	= lun;
    fUnit 	= fLUN->getFireWireUnit();
	fControl = fUnit->getController();
     
    fRefCon	= NULL;
	
    fLoginGeneration 		= 0;
    fLoginNodeID			= 0;
    fLoginFlags				= 0;
    fMaxPayloadSize			= 4;
    
    // NULL out fields we may allocate
	fStatusBlockAddressSpace 			= NULL;
	fStatusNotifyCallback				= NULL;
    fStatusNotifyRefCon					= NULL;
    fUnsolicitedStatusNotifyCallback 	= NULL;
    fUnsolicitedStatusNotifyRefCon	 	= NULL;

    fLoginORBAddressSpace 			= NULL;
	fLoginResponseAddressSpace 		= NULL;
	fLoginTimeoutTimerSet			= false;
    fLoginWriteCommand 				= NULL;
    fLoginWriteCommandMemory 		= NULL;
    fLoginWriteInProgress			= false;
	fLoginCompletionCallback 		= NULL;
    fLoginCompletionRefCon 			= NULL;

    fReconnectORBAddressSpace 			= NULL;
    fReconnectStatusBlockAddressSpace 	= NULL;
	fReconnectTime						= 0;
	fReconnectTimeoutTimerSet			= false;
	fReconnectWriteCommand 				= NULL;
    fReconnectWriteCommandMemory 		= NULL;
    fReconnectWriteInProgress 			= false;
    fReconnectWriteInterrupted			= false;
    
	fLogoutORBAddressSpace 			= NULL;
	fLogoutTimeoutTimerSet			= false;
	fLogoutWriteInProgress			= false;
	fLogoutWriteCommand 			= NULL;
    fLogoutWriteCommandMemory 		= NULL;
	fLogoutCompletionCallback		= NULL;
    fLogoutCompletionRefCon			= NULL;

	fFetchAgentWriteCommandInUse 	= false;
	fFetchAgentWriteCommand			= NULL;
    fFetchAgentWriteCommandMemory	= NULL;
	fFetchAgentWriteCompletion 		= NULL;
	
	fFetchAgentResetInProgress		= false;	
	fFetchAgentResetCommand 		= NULL;
	fFetchAgentResetRefCon 			= NULL;
	fFetchAgentResetCompletion		= NULL;

	fDoorbellInProgress		= false;
	fDoorbellRingAgain		= false;	
	fDoorbellCommand 		= NULL;

	fUnsolicitedStatusEnableInProgress		= false;	
	fUnsolicitedStatusEnableCommand 		= NULL;
	
	fSetBusyTimeoutBuffer = 0x0000000f;
	fSetBusyTimeoutInProgress = false;
	fSetBusyTimeoutAddress = FWAddress( 0x0000FFFF, 0xF0000210 );
	fSetBusyTimeoutCommand = NULL;

    fPasswordBuf = NULL;
    fPasswordLen = 0;
    fPasswordAddressSpace = NULL;
    fPasswordAddress = NULL;
	fPasswordDescriptor = NULL;
	fSuspended = false;
	
	//
	// set up command gate
	//
	
	IOWorkLoop * workLoop = NULL;
	if( status == kIOReturnSuccess )
	{
		workLoop = fLUN->getWorkLoop();
		if( !workLoop ) 
			status = kIOReturnNoResources;
	}
	
	if( status == kIOReturnSuccess )
	{
		fGate = IOCommandGate::commandGate( this );
		if( !fGate )
			status = kIOReturnNoMemory;
	}
	
	if( status == kIOReturnSuccess )
	{
		workLoop->retain();
		workLoop->addEventSource( fGate );
	}
	
	if( status == kIOReturnSuccess )
	{
		// init state machine
		fLoginState = kLoginStateIdle;
	
		// scan unit
		status = getUnitInformation();
		FWKLOG( ( "IOFireWireSBP2Login : fManagementOffset = 0x%lx, fMaxORBSize = %d, fMaxCommandBlockSize = %d, success = %d\n", fManagementOffset, fMaxORBSize, fMaxCommandBlockSize ) );
	}
	
	if( status == kIOReturnSuccess )
	{
		status = allocateResources();
	}

    if( status == kIOReturnSuccess )
    {
        fORBSet = OSSet::withCapacity(1);
		if( fORBSet == NULL )
			status = kIOReturnNoMemory;
	}
	
	if( status == kIOReturnSuccess )
	{
		fORBSetIterator = OSCollectionIterator::withCollection( fORBSet );
    }

    // the LUN that's creating us will call release() on a non-successful init
    // we'll clean up there from what ever half-allocated state we are in
    
    return (status == kIOReturnSuccess);
}

/////////////////////////////////////////////////////////////////////
// getUnitInformation
//
// gathers SBP2 specific information from unit's ROM
// specifically it reads, the management agent, and unit dependent
// characterisitcs

IOReturn IOFireWireSBP2Login::getUnitInformation( void )
{
    IOReturn			status = kIOReturnSuccess;

	//
	// get unit directory
    //

    IOConfigDirectory *		directory;
    
    status = fUnit->getConfigDirectory( directory );

    FWKLOG( ("IOFireWireSBP2Login : status = %d\n", status) );

	//
	// find managementOffset
	//

    if( status == kIOReturnSuccess )
    {
        // this is gross isn't it?
        FWAddress tempAddress;
        directory->getKeyOffset( kManagementAgentOffsetKey, tempAddress );
        fManagementOffset = (tempAddress.addressLo & 0x0fffffff) >> 2;            
    }
    
    FWKLOG( ("IOFireWireSBP2Login : status = %d, fManagementOffset = %d\n", status, fManagementOffset) );

    //
    // find unit characteristics
    //

    UInt32	unitCharacteristics;

    if( status == kIOReturnSuccess )
        status = directory->getKeyValue( kUnitCharacteristicsKey, unitCharacteristics );

    if( status == kIOReturnSuccess )
    {
        // extract management timeout, max ORB size, max command block size
        
        fManagementTimeout = ((unitCharacteristics >> 8) & 0xff) * 500;   // in milliseconds
		UInt32 orbSize = (unitCharacteristics & 0xff) * 4;
        if( orbSize < 32 )
            orbSize = 32;
        fMaxORBSize = orbSize;
        fMaxCommandBlockSize = orbSize;
    }
 
    return status;
}

/////////////////////////////////////////////////////////////////////
// allocateResources
//
// allocate addressSpaces (ORBs, status registers, response registers)
// and command objects for writing registers, etc.

IOReturn IOFireWireSBP2Login::allocateResources( void )
{
    IOReturn					status = kIOReturnSuccess;

    //
    // allocate and register an address space for the login ORB
    //

    if( status == kIOReturnSuccess )
    {
        fLoginORBAddressSpace = IOFWPseudoAddressSpace::simpleRead( fControl, &fLoginORBAddress, sizeof(FWSBP2LoginORB), &fLoginORB );
    	if ( fLoginORBAddressSpace == NULL )
        	status = kIOReturnNoMemory;
    }

    if( status == kIOReturnSuccess )
    {
        status = fLoginORBAddressSpace->activate();
    }
 
    //
    // allocate and register an address space for the login response
    //

    if( status == kIOReturnSuccess )
    {
        fLoginResponseAddressSpace = IOFWPseudoAddressSpace::simpleRW( fControl, &fLoginResponseAddress,
                                                                       sizeof(FWSBP2LoginResponse), 
																	   &fLoginResponse );
        if ( fLoginResponseAddressSpace == NULL )
            status = kIOReturnNoMemory;
    }

    if( status == kIOReturnSuccess )
    {
        status = fLoginResponseAddressSpace->activate();
    }

    //
    // allocate and register an address space for the reconnect ORB
    //

    if( status == kIOReturnSuccess )
    {
        fReconnectORBAddressSpace = IOFWPseudoAddressSpace::simpleRead( fControl, &fReconnectORBAddress, 
																		sizeof(FWSBP2ReconnectORB),
                                                                        &fReconnectORB );
    	if ( fReconnectORBAddressSpace == NULL )
            status = kIOReturnNoMemory;
    }

    if( status == kIOReturnSuccess )
    {
        status = fReconnectORBAddressSpace->activate();
    } 

    //
    // allocate and register an address space for the status block
    //

    if( status == kIOReturnSuccess )
    {
        fStatusBlockAddressSpace = fUnit->createPseudoAddressSpace( &fStatusBlockAddress, sizeof(FWSBP2StatusBlock),
                                                                    NULL, IOFireWireSBP2Login::statusBlockWriteStatic,
                                                                    this );
        if ( fStatusBlockAddressSpace == NULL )
            status = kIOReturnNoMemory;
    }

    if( status == kIOReturnSuccess )
    {
        status = fStatusBlockAddressSpace->activate();
    }

    //
    // allocate and register an address space for the reconnect status block
    //

    if( status == kIOReturnSuccess )
    {
        fReconnectStatusBlockAddressSpace =
            fUnit->createPseudoAddressSpace( &fReconnectStatusBlockAddress, sizeof(FWSBP2StatusBlock),
                                             NULL, IOFireWireSBP2Login::reconnectStatusBlockWriteStatic, this );
        
        if ( fReconnectStatusBlockAddressSpace == NULL )
            status = kIOReturnNoMemory;
    }

    if( status == kIOReturnSuccess )
    {
        status = fReconnectStatusBlockAddressSpace->activate();
    }

    //
    // allocate and register an address space for the logout ORB
    //

    if( status == kIOReturnSuccess )
    {
        fLogoutORBAddressSpace = IOFWPseudoAddressSpace::simpleRead( fControl, &fLogoutORBAddress, sizeof(FWSBP2LogoutORB),
                                                                     &fLogoutORB );
    	if ( fLogoutORBAddressSpace == NULL )
        	status = kIOReturnNoMemory;
    }
  
    if( status == kIOReturnSuccess )
    {
        status = fLogoutORBAddressSpace->activate();
    }

    //
    // prepare parts of the ORBs
    //
    
    fLoginORB.loginResponseAddressHi = (fLoginResponseAddress.nodeID << 16) | fLoginResponseAddress.addressHi;
    fLoginORB.loginResponseAddressLo = fLoginResponseAddress.addressLo;
    fLoginORB.loginResponseLength = sizeof( FWSBP2LoginResponse );
    fLoginORB.lun = fLUN->getLUNumber();
    FWKLOG( ("lun number = %d\n", fLoginORB.lun) );
    fLoginORB.statusFIFOAddressHi = (fStatusBlockAddress.nodeID << 16) | fStatusBlockAddress.addressHi;
    fLoginORB.statusFIFOAddressLo = fStatusBlockAddress.addressLo;

    fReconnectORB.options = 3 |  0x8000;   // reconnect | notify
    fReconnectORB.statusFIFOAddressHi = (fReconnectStatusBlockAddress.nodeID << 16) | fReconnectStatusBlockAddress.addressHi;
    fReconnectORB.statusFIFOAddressLo = fReconnectStatusBlockAddress.addressLo;

    fLogoutORB.options = 7 | 0x8000;  	// logout | notify
    fLogoutORB.statusFIFOAddressHi = (fStatusBlockAddress.nodeID << 16) | fStatusBlockAddress.addressHi;
    fLogoutORB.statusFIFOAddressLo = fStatusBlockAddress.addressLo;
    
	//
    // create command for writing the management agent
    //
    
    if( status == kIOReturnSuccess )
    {
        fLoginWriteCommandMemory = IOMemoryDescriptor::withAddress( &fLoginORBAddress, 8, kIODirectionOut);
    	if( fLoginWriteCommandMemory == NULL )
    		status = kIOReturnNoMemory;
    }

    if( status == kIOReturnSuccess )
    {
        fLoginWriteCommand = fUnit->createWriteCommand( FWAddress(0x0000ffff, 0xf0000000 + (fManagementOffset << 2)),
                                                        fLoginWriteCommandMemory,
                                                        IOFireWireSBP2Login::loginWriteCompleteStatic, this, true );
        if( fLoginWriteCommand == NULL )
        	status = kIOReturnNoMemory;
    }

    if( status == kIOReturnSuccess )
    {
		fLoginTimeoutCommand = fControl->createDelayedCmd( fManagementTimeout * 1000,
                                                           IOFireWireSBP2Login::loginTimeoutStatic, this);
	}

    //
    // create command for writing the management agent during reconnect
    //
    
    if( status == kIOReturnSuccess )
    {
        fReconnectWriteCommandMemory = IOMemoryDescriptor::withAddress( &fReconnectORBAddress, 8, kIODirectionOut);
    	if( fReconnectWriteCommandMemory == NULL )
    		status = kIOReturnNoMemory;
    }

    if( status == kIOReturnSuccess )
    {
        fReconnectWriteCommand = fUnit->createWriteCommand( FWAddress(0x0000ffff, 0xf0000000 + (fManagementOffset << 2)),
                                                            fReconnectWriteCommandMemory,
                                                            IOFireWireSBP2Login::reconnectWriteCompleteStatic, this, true );
        if( fReconnectWriteCommand == NULL )
        	status = kIOReturnNoMemory;
    }

   if( status == kIOReturnSuccess )
    {
		fReconnectTimeoutCommand = (IOFWDelayCommand*)fControl->createDelayedCmd( ((fManagementTimeout + 1000) * 1000),
                                                                IOFireWireSBP2Login::reconnectTimeoutStatic, this);
		if( !fReconnectTimeoutCommand )
			status = kIOReturnNoMemory;
	}

    //
    // create command for writing the management agent during logout
    //
    
    if( status == kIOReturnSuccess )
    {
        fLogoutWriteCommandMemory = IOMemoryDescriptor::withAddress( &fLogoutORBAddress, 8, kIODirectionOut);
    	if( fLogoutWriteCommandMemory == NULL )
    		status = kIOReturnNoMemory;
    }

    if( status == kIOReturnSuccess )
    {
        fLogoutWriteCommand = fUnit->createWriteCommand( FWAddress(0x0000ffff, 0xf0000000 + (fManagementOffset << 2)),
                                                         fLogoutWriteCommandMemory,
                                                         IOFireWireSBP2Login::logoutWriteCompleteStatic, this, true );
        if( fLogoutWriteCommand == NULL )
        	status = kIOReturnNoMemory;
    }

	//
	// create command timeout during logout
	//

    if( status == kIOReturnSuccess )
    {
		fLogoutTimeoutCommand = fControl->createDelayedCmd( fManagementTimeout * 1000,
                                                            IOFireWireSBP2Login::loginTimeoutStatic, this);
	}

    //
    // create command for writing the fetch agent
    //
    
    if( status == kIOReturnSuccess )
    {
        fFetchAgentWriteCommandMemory = IOMemoryDescriptor::withAddress( &fLastORBAddress, 8, kIODirectionOut );
    	if( fFetchAgentWriteCommandMemory == NULL )
    		status = kIOReturnNoMemory;
    }

    if( status == kIOReturnSuccess )
    {
        fFetchAgentWriteCommand = fUnit->createWriteCommand( FWAddress(0,0),  // tbd later
                                                             fFetchAgentWriteCommandMemory,
                                                             IOFireWireSBP2Login::fetchAgentWriteCompleteStatic, this, true );
        if( fFetchAgentWriteCommand == NULL )
        	status = kIOReturnNoMemory;
    }

	//
    // create command for reseting the fetch agent
    //
    
    if( status == kIOReturnSuccess )
    {
        fFetchAgentResetCommand = fUnit->createWriteQuadCommand( FWAddress(0,0),  // tbd later
                                                             &fFetchAgentResetBuffer, 1,
                                                             IOFireWireSBP2Login::fetchAgentResetCompleteStatic, 
															 this, true );
        if( fFetchAgentResetCommand == NULL )
        	status = kIOReturnNoMemory;
    }
    
	//
    // create command for ringing the doorbell
    //
    
    if( status == kIOReturnSuccess )
    {
        fDoorbellCommand = fUnit->createWriteQuadCommand( FWAddress(0,0),  // tbd later
                                                             &fDoorbellBuffer, 1,
                                                             IOFireWireSBP2Login::doorbellCompleteStatic, 
															 this, true );
        if( fDoorbellCommand == NULL )
        	status = kIOReturnNoMemory;
    }

	//
    // create command for setting busy timeout
    //
    
    if( status == kIOReturnSuccess )
    {
        fSetBusyTimeoutCommand = fUnit->createWriteQuadCommand( fSetBusyTimeoutAddress,
                                                             &fSetBusyTimeoutBuffer, 1,
                                                             IOFireWireSBP2Login::setBusyTimeoutCompleteStatic, 
															 this, true );
        if( fSetBusyTimeoutCommand == NULL )
        	status = kIOReturnNoMemory;
    }

	//
    // create command for enabling unsolicited status
    //
    
    if( status == kIOReturnSuccess )
    {
        fUnsolicitedStatusEnableCommand = fUnit->createWriteQuadCommand( FWAddress(0,0),  // tbd later
                                                             &fUnsolicitedStatusEnableBuffer, 1,
                                                             IOFireWireSBP2Login::unsolicitedStatusEnableCompleteStatic, 
															 this, true );
        if( fUnsolicitedStatusEnableCommand == NULL )
        	status = kIOReturnNoMemory;
    }

    return status;
}

void IOFireWireSBP2Login::release() const
{
	if( getRetainCount() >= 2 ) 
		OSObject::release(2);
}

void IOFireWireSBP2Login::free( void )
{
    IOReturn						status = kIOReturnSuccess;

    FWKLOG( ( "IOFireWireSBP2Login : free called\n" ) );

    // disconnect from lun
    removeLogin();

	///////////////////////////////////////////
    
	//
	// release command for reseting fetch agent
	//
	
	if( fFetchAgentResetInProgress )
		fFetchAgentResetCommand->cancel( kIOReturnAborted );
		
	if( fFetchAgentResetCommand )
		fFetchAgentResetCommand->release();

	//
	// release command for ringing the doorbell
	//
	
	if( fDoorbellInProgress )
		fDoorbellCommand->cancel( kIOReturnAborted );
		
	if( fDoorbellCommand )
		fDoorbellCommand->release();

	//
	// release command for enabling unsolicited status
	//
	
	if( fUnsolicitedStatusEnableInProgress )
		fUnsolicitedStatusEnableCommand->cancel( kIOReturnAborted );
		
	if( fUnsolicitedStatusEnableCommand )
		fUnsolicitedStatusEnableCommand->release();
	
	//
	// release command for setting busy timeout
	//
	
	if( fSetBusyTimeoutInProgress )
		fSetBusyTimeoutCommand->cancel( kIOReturnAborted );
		
	if( fSetBusyTimeoutCommand )
		fSetBusyTimeoutCommand->release();
		
    //
    // release command for writing the management agent
    //

    if( fLoginWriteInProgress )
        fLoginWriteCommand->cancel( kIOReturnAborted );

    if( fLoginWriteCommand != NULL )
        fLoginWriteCommand->release();

    if( fLoginWriteCommandMemory != NULL )
        fLoginWriteCommandMemory->release();

    // cancel timer
    if( fLoginTimeoutTimerSet )
        fLoginTimeoutCommand->cancel(kIOReturnAborted);

	if( fLoginTimeoutCommand )
		fLoginTimeoutCommand->release();

    //
    // deallocate login orb address space
    //

    if( fLoginORBAddressSpace != NULL && status == kIOReturnSuccess )
    {
        fLoginORBAddressSpace->deactivate();
        fLoginORBAddressSpace->release();
    }

    //
    // deallocate login response address space
    //

    if( fLoginResponseAddressSpace != NULL && status == kIOReturnSuccess )
    {
        fLoginResponseAddressSpace->deactivate();
        fLoginResponseAddressSpace->release();
    }

	///////////////////////////////////////////

    //
    // release command for writing the management agent during reconnect
    //

    if( fReconnectWriteInProgress )
        fReconnectWriteCommand->cancel(kIOReturnAborted);

    if( fReconnectWriteCommand != NULL )
        fReconnectWriteCommand->release();

    if( fReconnectWriteCommandMemory != NULL )
        fReconnectWriteCommandMemory->release();

    // cancel timer
    if( fReconnectTimeoutTimerSet )
        fReconnectTimeoutCommand->cancel(kIOReturnAborted);

	if( fReconnectTimeoutCommand )
		fReconnectTimeoutCommand->release();

    //
    // deallocate reconnect orb address space
    //

    if( fReconnectORBAddressSpace != NULL && status == kIOReturnSuccess )
    {
        fReconnectORBAddressSpace->deactivate();
        fReconnectORBAddressSpace->release();
     }

    //
    // deallocate reconnect status block address space
    //

    if( fReconnectStatusBlockAddressSpace != NULL && status == kIOReturnSuccess )
    {
        fReconnectStatusBlockAddressSpace->deactivate();
        fReconnectStatusBlockAddressSpace->release();
    }
  
  	///////////////////////////////////////////
  
    //
    // release command for writing the management agent during logout
    //

    if( fLogoutWriteInProgress )
        fLogoutWriteCommand->cancel(kIOReturnAborted);

    if( fLogoutWriteCommand != NULL )
        fLogoutWriteCommand->release();

    if( fLogoutWriteCommandMemory != NULL )
        fLogoutWriteCommandMemory->release();

	// cancel timer
    if( fLogoutTimeoutTimerSet )
        fLogoutTimeoutCommand->cancel(kIOReturnAborted);

	// release logout timer
	if( fLogoutTimeoutCommand )
		fLogoutTimeoutCommand->release();

    //
    // deallocate logout orb address space
    //

    if( fLogoutORBAddressSpace != NULL && status == kIOReturnSuccess )
    {
        fLogoutORBAddressSpace->deactivate();
        fLogoutORBAddressSpace->release();
     }

	///////////////////////////////////////////

    //
    // release command for writing the fetch agent
    //

    if( fFetchAgentWriteCommandInUse )
        fFetchAgentWriteCommand->cancel(kIOReturnAborted);

    if( fFetchAgentWriteCommand != NULL )
        fFetchAgentWriteCommand->release();

    if( fFetchAgentWriteCommandMemory != NULL )
        fFetchAgentWriteCommandMemory->release();
     
    
    //
    // deallocate status block address space
    //

    if( fStatusBlockAddressSpace != NULL && status == kIOReturnSuccess )
    {
        fStatusBlockAddressSpace->deactivate();
        fStatusBlockAddressSpace->release();
    }

    //
    // deallocate old password address space
    //

    if( fPasswordAddressSpace != NULL )
    {
        fPasswordAddressSpace->deactivate();
        fPasswordAddressSpace->release();
    }

	//
	// free unreleased orbs
	//
	
	if( fORBSetIterator )
	{
		IOFireWireSBP2ORB * item = NULL;
		do
		{
            fORBSetIterator->reset();	
			item = (IOFireWireSBP2ORB *)fORBSetIterator->getNextObject();
			if( item )
				item->release();
		} while( item );
		
		fORBSetIterator->release();
	}
	
	if( fORBSet )
		fORBSet->release();
    
	//
	// destroy command gate
	//
	
	if( fGate != NULL )
	{
		IOWorkLoop * workLoop = NULL;

		workLoop = fGate->getWorkLoop();
		workLoop->removeEventSource( fGate );
		workLoop->release();

		fGate->release();
		fGate = NULL;
	}

	OSObject::free();
}

/////////////////////////////////////////////////////////////////////
// accessors
//

// get unit

IOFireWireUnit * IOFireWireSBP2Login::getFireWireUnit( void )
{
    return fUnit;
}

// get lun

IOFireWireSBP2LUN * IOFireWireSBP2Login::getFireWireLUN( void )
{
    return fLUN;
}

// get command block size

UInt32 IOFireWireSBP2Login::getMaxCommandBlockSize( void )
{
    return fMaxCommandBlockSize;
}

UInt32 IOFireWireSBP2Login::getLoginID( void )
{
    return fLoginID;
}

// get / set refCon

void IOFireWireSBP2Login::setRefCon( void * refCon )
{
    fRefCon = refCon;
}

void * IOFireWireSBP2Login::getRefCon( void )
{
    return fRefCon;
}

// get / set login flags

void IOFireWireSBP2Login::setLoginFlags( UInt32 loginFlags )
{
    fLoginFlags = loginFlags;
    FWKLOG(( "IOFireWireSBP2Login : setLoginFlags : 0x%08lx\n", fLoginFlags ));
}

UInt32 IOFireWireSBP2Login::getLoginFlags( void )
{
    return fLoginFlags;
}

// set login and logout completion routines

void IOFireWireSBP2Login::setLoginCompletion( void * refCon, FWSBP2LoginCallback completion )
{
    fLoginCompletionRefCon = refCon;
    fLoginCompletionCallback = completion;
}

void IOFireWireSBP2Login::setLogoutCompletion( void * refCon, FWSBP2LogoutCallback completion )
{
    fLogoutCompletionRefCon = refCon;
    fLogoutCompletionCallback = completion;
}

// get / set reconnect time

void IOFireWireSBP2Login::setReconnectTime( UInt32 reconnectTime )
{
    fReconnectTime = reconnectTime & 0x0000000f;
}

UInt32 IOFireWireSBP2Login::getReconnectTime( void )
{
    return fReconnectTime;
}

// get / set status notify proc

void IOFireWireSBP2Login::setStatusNotifyProc( void * refCon, FWSBP2NotifyCallback callback )
{
    fStatusNotifyCallback = callback;
    fStatusNotifyRefCon = refCon;
}

void IOFireWireSBP2Login::getStatusNotifyProc( void ** refCon, FWSBP2NotifyCallback * callback )
{
    *callback = fStatusNotifyCallback;
    *refCon = fStatusNotifyRefCon;
}

// get / set status notify proc

void IOFireWireSBP2Login::setUnsolicitedStatusNotifyProc( void * refCon, FWSBP2NotifyCallback callback )
{
    fUnsolicitedStatusNotifyCallback = callback;
    fUnsolicitedStatusNotifyRefCon = refCon;
}

void IOFireWireSBP2Login::getUnsolicitedStatusNotifyProc( void ** refCon, FWSBP2NotifyCallback * callback )
{
    *callback = fStatusNotifyCallback;
    *refCon = fStatusNotifyRefCon;
}

// get / set max payload size

void IOFireWireSBP2Login::setMaxPayloadSize( UInt32 maxPayloadSize )
{
    fMaxPayloadSize = maxPayloadSize;
}

UInt32 IOFireWireSBP2Login::getMaxPayloadSize( void )
{
    return fMaxPayloadSize;
}

// set password
//
//

IOReturn IOFireWireSBP2Login::setPassword( IOMemoryDescriptor * memory )
{
    IOReturn status = kIOReturnSuccess;
	IOByteCount len = 0;
	
    //
    // deallocate old password address space
    //
	
    if( fPasswordAddressSpace != NULL )
    {
        fPasswordAddressSpace->deactivate();
        fPasswordAddressSpace->release();
        fPasswordBuf = NULL;
        fPasswordLen = 0;
    }

	if( fPasswordDescriptor != NULL )
	{
		fPasswordDescriptor->release();
		fPasswordDescriptor = NULL;
	}
	
	if( status == kIOReturnSuccess )
	{
		fPasswordDescriptor = memory;
		if( memory == NULL )
			status = kIOReturnError;
	}
	
	if( status == kIOReturnSuccess )
	{
		fPasswordDescriptor->retain();
		len = fPasswordDescriptor->getLength();
	}
	
	if( status == kIOReturnSuccess )
	{	
		if( len <= 8 )
		{
			fLoginORB.password[0] = 0;
			fLoginORB.password[1] = 0;
			fLoginORB.passwordLength = 0;			
			status = memory->readBytes( 0, &(fLoginORB.password), len );
		}
		else
		{
	
			//
			// allocate and register an address space for the password
			//
	
			if( status == kIOReturnSuccess )
			{
				fPasswordAddressSpace = IOFWPseudoAddressSpace::simpleRW( fControl, &fPasswordAddress, memory );
				if( fPasswordAddressSpace == NULL )
					status = kIOReturnNoMemory;
			}
	
			if( status == kIOReturnSuccess )
			{
				status = fPasswordAddressSpace->activate();
			}
	
			if( status == kIOReturnSuccess )
			{
				fLoginORB.passwordLength = len;
				fLoginORB.password[0] = 0x0000ffff & fPasswordAddress.addressHi;
				fLoginORB.password[1] = fPasswordAddress.addressLo;
			}
			
		}
    }
	
    return status;
}

// set password
//
//

IOReturn IOFireWireSBP2Login::setPassword( void * buf, UInt32 len )
{
    IOReturn						status 			= kIOReturnSuccess;

    //
    // deallocate old password address space
    //

	
    if( fPasswordAddressSpace != NULL )
    {
        fPasswordAddressSpace->deactivate();
        fPasswordAddressSpace->release();
        fPasswordBuf = NULL;
        fPasswordLen = 0;
    }

	if( fPasswordDescriptor != NULL )
	{
		fPasswordDescriptor->release();
		fPasswordDescriptor = NULL;
	}
	
    if( len <= 8 )
    {
        fPasswordBuf = buf;
        fPasswordLen = len;

		fLoginORB.password[0] = 0;
		fLoginORB.password[1] = 0;
		
        bcopy( buf, &(fLoginORB.password), len );
        fLoginORB.passwordLength = 0;
    }
    else
    {

        //
        // allocate and register an address space for the password
        //

        if( status == kIOReturnSuccess )
        {
			fPasswordAddressSpace = IOFWPseudoAddressSpace::simpleRW( fControl, &fPasswordAddress, len, buf );
			if( fPasswordAddressSpace == NULL )
				status = kIOReturnNoMemory;
        }

        if( status == kIOReturnSuccess )
        {
            status = fPasswordAddressSpace->activate();
        }

        if( status == kIOReturnSuccess )
        {
            fPasswordBuf = buf;
            fPasswordLen = len;
            fLoginORB.passwordLength = len;
            fLoginORB.password[0] = 0x0000ffff & fPasswordAddress.addressHi;
            fLoginORB.password[1] = fPasswordAddress.addressLo;
        }
        
    }
    
    return status;
}


/////////////////////////////////////////////////////////////////////

void IOFireWireSBP2Login::completeLogin( IOReturn state, const void *buf, UInt32 len, void * buf2 )
{
	FWKLOG( ( "IOFireWireSBP2Login : completeLogin\n" ) );
	
	if( fLoginCompletionCallback != NULL )
    {
        FWSBP2LoginCompleteParams		params;

        params.login = this;
        params.generation = fLoginGeneration;

        params.status = state;
        params.loginResponse = (FWSBP2LoginResponsePtr)buf2;
        params.statusBlock = (FWSBP2StatusBlock*)buf;
        params.statusBlockLength = len;
        
         (*fLoginCompletionCallback)(fLoginCompletionRefCon, &params);        
    }
}

void IOFireWireSBP2Login::completeLogout( IOReturn state, const void *buf, UInt32 len )
{
	FWKLOG( ( "IOFireWireSBP2Login : completeLogout\n" ) );

    if( fLogoutCompletionCallback != NULL )
    {
        FWSBP2LogoutCompleteParams		params;

        params.login = this;
        params.generation = fLoginGeneration;

        params.status = state;
        params.statusBlock = (FWSBP2StatusBlock*)buf;
        params.statusBlockLength = len;

        (*fLogoutCompletionCallback)(fLogoutCompletionRefCon, &params);
    }
}

/////////////////////////////////////////////////////////////////////
//
// login path
//

// submitLogin
//
//

IOReturn IOFireWireSBP2Login::submitLogin( void )
{
    IOReturn status = kIOReturnSuccess;
	
	status = fGate->runAction( staticExecuteLogin );
	
	return status;
}

IOReturn IOFireWireSBP2Login::staticExecuteLogin( OSObject *self, void *, void *, void *, void * )
{
	return ((IOFireWireSBP2Login *)self)->executeLogin();
}

IOReturn IOFireWireSBP2Login::executeLogin( void )
{
    IOReturn status = kIOReturnSuccess;
    
	FWKLOG( ( "IOFireWireSBP2Login : executeLogin\n" ) );

	if( fSuspended )
	{
#if 1
        // this might break Craig if I change it
		completeLogin( kIOReturnError );
        return kIOReturnSuccess;
#else
		return kIOReturnError;
#endif
	}
	
	if( fLoginState != kLoginStateIdle )
	{
		return kIOReturnExclusiveAccess;
	}
	
    fLoginState = kLoginStateLoggingIn;  		// set state
    
	// to quote sbp-2 : ... truncated login response data 
	// shall be interpreted as if the omitted fields
	// had been stored as zeros. ... 
	
    fLoginResponse.reserved = 0;
    fLoginResponse.reconnectHold = 0;

    // set options
    FWKLOG(( "IOFireWireSBP2Login : fLoginFlags : 0x%08lx\n", fLoginFlags ));

    fLoginORB.options = 0x0000 | 0x8000;		// login | notify
    fLoginORB.options |= (fReconnectTime << 4);
    if( fLoginFlags & kFWSBP2ExclusiveLogin )
        fLoginORB.options |= 0x1000;

    // set to correct generation
    fLoginWriteCommand->reinit( FWAddress(0x0000ffff, 0xf0000000 + (fManagementOffset << 2)),
                                fLoginWriteCommandMemory, IOFireWireSBP2Login::loginWriteCompleteStatic,
                                this, true );

    // get local node and generation
    // note: these two are latched affter command gen is set and before submit is called
    // if these are old command will fail
    UInt16 unitNode; // Dummy
    fUnit->getNodeIDGeneration(fLoginGeneration, unitNode, fLoginNodeID);
    fLoginWriteInProgress = true;
    status = fLoginWriteCommand->submit();
    if( status != kIOReturnSuccess )
    {
        fLoginWriteInProgress = false;
        fLoginState = kLoginStateIdle;
        return status;
    }
    
    return kIOReturnSuccess;
}

//
// login write completion
//

void IOFireWireSBP2Login::loginWriteCompleteStatic( void *refcon, IOReturn status, IOFireWireNub *device,
                                                    IOFWCommand *fwCmd )
{
    ((IOFireWireSBP2Login*)refcon)->loginWriteComplete( status, device, fwCmd );
}

void IOFireWireSBP2Login::loginWriteComplete( IOReturn status, IOFireWireNub *device, IOFWCommand *fwCmd )
{
    FWKLOG( ("IOFireWireSBP2Login : login write complete\n") );

    fLoginWriteInProgress = false;
    
    if( status == kIOReturnSuccess )
    {
        // we wrote the management agent, now set a timer and wait for response & status
        fLoginTimeoutTimerSet = true;
		if( fLoginTimeoutCommand->submit() != kIOReturnSuccess )
			fLoginTimeoutTimerSet = false;  
	}
    else
    {
        fLoginState = kLoginStateIdle;
        completeLogin( status );  // complete with error
    }
}

//
// login timeout
//

void IOFireWireSBP2Login::loginTimeoutStatic( void *refcon, IOReturn status,
                                               IOFireWireBus *bus, IOFWBusCommand *fwCmd)
{
    ((IOFireWireSBP2Login*)refcon)->loginTimeout( status, bus, fwCmd );
}

void IOFireWireSBP2Login::loginTimeout( IOReturn status, IOFireWireBus *bus, IOFWBusCommand *fwCmd)
{
    fLoginTimeoutTimerSet = false;
    if(status == kIOReturnTimeout)
    {
        FWKLOG( ("IOFireWireSBP2Login : login timed out\n") );
        
        // init state machine
        fLoginState = kLoginStateIdle;
        completeLogin( kIOReturnTimeout );
    }
}

void IOFireWireSBP2Login::abortLogin( void )
{
    // if we're in the middle of a login write, we need to cancel it
    // before aborting the command
    if( fLoginWriteInProgress )
        fLoginWriteCommand->cancel( kIOReturnAborted );

    // cancel timeout
    if( fLoginTimeoutTimerSet )
        fLoginTimeoutCommand->cancel( kIOReturnAborted );

    // set state
    fLoginState = kLoginStateIdle;

    // complete command
    completeLogin( kIOReturnTimeout );  //zzz is this a good error?, this is what OS9 returns
}

//
// status block handlers
//

UInt32 IOFireWireSBP2Login::statusBlockWriteStatic(void *refcon, UInt16 nodeID, IOFWSpeed &speed,
                FWAddress addr, UInt32 len, const void *buf, IOFWRequestRefCon lockRead)
{
   	return ((IOFireWireSBP2Login*)refcon)->statusBlockWrite( nodeID, speed, addr, len, buf, lockRead );
}

UInt32 IOFireWireSBP2Login::statusBlockWrite( UInt16 nodeID, IOFWSpeed &speed, FWAddress addr, UInt32 len,
                                              const void *buf, IOFWRequestRefCon lockRead )
{
    FWKLOG( ( "IOFireWireSBP2Login : status block write\n" ) );

    //�����������������
    if( len < sizeof(fStatusBlock) )
        bcopy( buf, &fStatusBlock, len);
    else
        bcopy( buf, &fStatusBlock, sizeof(fStatusBlock));

    switch( fLoginState )
    {
        case kLoginStateLoggingIn:

            // complete login

            // cancel timeout
            if( fLoginTimeoutTimerSet )
            {
                fLoginTimeoutCommand->cancel(kIOReturnAborted);
            }

            // success or not
            if( ((fStatusBlock.details >> 4) & 3) == kFWSBP2RequestComplete &&
                fStatusBlock.sbpStatus == kFWSBP2NoSense )
            {
                FWKLOG( ( "IOFireWireSBP2Login : successful login\n" ) );
  
                // get login ID and fetch agent address
                fLoginID = fLoginResponse.loginID;
                fReconnectORB.loginID = fLoginID;  // set id for reconnect;
            
                // set reconnect_hold, some devices indicate it
                if( fLoginResponse.length >= 16 )
                    fReconnectHold = (fLoginResponse.reconnectHold & 0x7fff) + 1;
                else
                    fReconnectHold = 1;

				// set fetch agent reset address
				fFetchAgentResetAddress = FWAddress( fLoginResponse.commandBlockAgentAddressHi & 0x0000ffff,
													 fLoginResponse.commandBlockAgentAddressLo + 0x00000004 );
				
				fFetchAgentResetCommand->reinit( fFetchAgentResetAddress,
												&fFetchAgentResetBuffer, 1,
												IOFireWireSBP2Login::fetchAgentResetCompleteStatic, 
												this, true );

                // set fetch agent address
                fFetchAgentAddress = FWAddress( fLoginResponse.commandBlockAgentAddressHi & 0x0000ffff,
                                                fLoginResponse.commandBlockAgentAddressLo + 0x00000008);

                fFetchAgentWriteCommand->reinit( fFetchAgentAddress,  // tis determined
                                                 fFetchAgentWriteCommandMemory,
                                                 IOFireWireSBP2Login::fetchAgentWriteCompleteStatic, this, true );
                
				// set doorbell reset address
				fDoorbellAddress = FWAddress( fLoginResponse.commandBlockAgentAddressHi & 0x0000ffff,
											  fLoginResponse.commandBlockAgentAddressLo + 0x00000010 );
				
				fDoorbellCommand->reinit( fDoorbellAddress,
										  &fDoorbellBuffer, 1,
										  IOFireWireSBP2Login::doorbellCompleteStatic, 
										  this, true );

				// set unsolicited status enable address
				fUnsolicitedStatusEnableAddress = FWAddress( fLoginResponse.commandBlockAgentAddressHi & 0x0000ffff,
															 fLoginResponse.commandBlockAgentAddressLo + 0x00000014 );
				
				fUnsolicitedStatusEnableCommand->reinit( fUnsolicitedStatusEnableAddress,
														 &fUnsolicitedStatusEnableBuffer, 1,
														 IOFireWireSBP2Login::unsolicitedStatusEnableCompleteStatic, 
														 this, true );

				fLoginState = kLoginStateConnected;
				
                completeLogin( kIOReturnSuccess, buf, len, &fLoginResponse );
            }
            else
            {
                FWKLOG( ( "IOFireWireSBP2Login : login failed\n" ) );
                fLoginState = kLoginStateIdle;

                completeLogin( kIOReturnError, buf, len, NULL );
            }
            break;

        case kLoginStateConnected:
            // orb related or unsolicited
            
            if( ((*((UInt8*)buf)) & 0xc0) == 0x80 )
            {
                // send unsolicited status
                if( fUnsolicitedStatusNotifyCallback != NULL )
                {
                    FWSBP2NotifyParams		params;

                    params.message = buf;
                    params.length = len;
                    params.notificationEvent = kFWSBP2UnsolicitedStatus;
                    params.generation = fLoginGeneration;
                    (*fUnsolicitedStatusNotifyCallback)(fUnsolicitedStatusNotifyRefCon, &params );
                }

            }
            else
            {
                // normal command orb

                // find ORB
                bool found = false;
                FWAddress notificationAddress = *((FWAddress*)buf);
                IOFireWireSBP2ORB * item = NULL;
                fORBSetIterator->reset();
                while( (found != true) && (item = (IOFireWireSBP2ORB *) fORBSetIterator->getNextObject())  )
                {
                    FWAddress address;
                    item->getORBAddress( &address );
                    if( (notificationAddress.addressHi & 0x0000ffff) == (address.addressHi & 0x0000ffff) &&
                         notificationAddress.addressLo == address.addressLo ) 
                    {
                        found = true;
                    }                   
                }
                
                FWKLOG( ( "IOFireWireSBP2Login : solicited found = %d\n", found ) );
				
				FWSBP2StatusBlock * statusBlock = (FWSBP2StatusBlock*)buf;
				bool deadBitIsSet = statusBlock->details & 0x08;
                
                if( found )
                {
                    // cancel timer if set
                    if( isORBTimerSet( item ) )
                    {
                        cancelORBTimer( item);
                    }
                    else if( item->getCommandTimeout() != 0 )
                    {
                        // if timer went off and then we get status, just ignore status.
                        // late arriving status will be discarded unless timeout duration is changed to zero
                        // at which point we can't tell that there ever was a timer running
                        break;
                    }
					
					// send solicited status
                                    
                    if( fStatusNotifyCallback != NULL )
                    {
                        FWSBP2NotifyParams		params;
     
                        params.message = buf;
                        params.length = len;
                        params.notificationEvent = kFWSBP2NormalCommandStatus;
                        params.generation = fLoginGeneration;
                        params.commandObject = item;
                        (*fStatusNotifyCallback)(fStatusNotifyRefCon, &params );
                    }
                }
				
				if( deadBitIsSet )
				{
					// all tasks are aborted
					clearAllTasksInSet();
				}
            }
            
            break;

        case kLoginStateLoggingOut:

            // cancel timeout
            if( fLogoutTimeoutTimerSet )
            {
                fLogoutTimeoutCommand->cancel(kIOReturnAborted);
                fLogoutTimeoutTimerSet = false;
            }
       
            // success or not
            if( ((fStatusBlock.details >> 4) & 3) == kFWSBP2RequestComplete &&
                fStatusBlock.sbpStatus == kFWSBP2NoSense )
            {
                FWKLOG( ( "IOFireWireSBP2Login : successful logout\n" ) );
                fLoginState = kLoginStateIdle;

                // get login ID and fetch agent address
                fLoginID = fLoginResponse.loginID;
                completeLogout( kIOReturnSuccess, buf, len );
            }
            else
            {
                FWKLOG( ( "IOFireWireSBP2Login : logout failed!?\n" ) );
                fLoginState = kLoginStateIdle;
                completeLogout( kIOReturnError, buf, len );
            }
			
			// all tasks are aborted
			clearAllTasksInSet();
			
            break;

        case kLoginStateReconnect:
        case kLoginStateIdle:
        default:
            FWKLOG( ( "IOFireWireSBP2Login : status block write on illegal state\n" ) );
            break;
    }

    return kFWResponseComplete;
}

/////////////////////////////////////////////////////////////////////
//
// reconnect path
//

//
// suspendedNotify method
//
// called when a suspended message is received

void IOFireWireSBP2Login::suspendedNotify( void )
{
    FWKLOG( ( "IOFireWireSBP2Login : suspendedNotify\n" ) );
    
	fSuspended = true;
	
    switch( fLoginState )
    {
        case kLoginStateConnected:
            // start/restart timer
            startReconnectTimer();
            break;

        case kLoginStateReconnect:
			// start/restart timer
			startReconnectTimer();
            break;

        case kLoginStateLoggingIn:
            abortLogin();
            break;

        case kLoginStateIdle:
        case kLoginStateLoggingOut:
        default:
            FWKLOG( ("IOFireWireSBP2Login : suspended notify, nothing to do\n") );
            break;
    }
}

//
// resumeNotify method
//
// called when a resume message is received

void IOFireWireSBP2Login::resumeNotify( void )
{
    FWKLOG( ( "IOFireWireSBP2Login : resume notify\n" ) );

	fSuspended = false;
	
	executeSetBusyTimeout(); //can handle interruption
	
	if( fLogoutPending )
	{
		fLogoutPending = false;
		executeLogout();
	}
	else
	{
		switch( fLoginState )
		{
			case kLoginStateConnected:
				doReconnect();
				break;
							
			case kLoginStateReconnect:
				// restart timer
				restartReconnect();
				break;
	
			case kLoginStateLoggingIn:
				abortLogin();
				break;
	
			case kLoginStateIdle:            
			case kLoginStateLoggingOut:
			default:
				FWKLOG( ("IOFireWireSBP2Login : resume notify, nothing to do\n") );
				break;
		}
	}
}

// startReconnectTimer
//
//

void IOFireWireSBP2Login::startReconnectTimer( void )
{
    fLoginState = kLoginStateReconnect;
    
    if( fReconnectTimeoutTimerSet )
         fReconnectTimeoutCommand->cancel(kIOReturnAborted);
 
    // start a reconnect timer

    // even if a device went away
    // we want to keep trying for up to the reconnect timeout. maybe we'll get
    // another bus reset and the device will be back, and still willing to reconnect

    // this assumes that transient errors might cause us to loose sight of the device
    // for short periods (shorter than the timeout).  if the reconnect timeout is
    // really large (several seconds) then maybe we'll wait too long.

    // zzz some devices wait until the end of the reconnect interval before they
    // acknowledge a reconnect.  that seems bad, but for now add one second of fudge
    // factor so we can tolerate this.

	fReconnectTimeoutCommand->reinit( ((fManagementTimeout + 1000) * 1000),
									  IOFireWireSBP2Login::reconnectTimeoutStatic, this);

    // we wrote the management agent, now set a timer and wait for response & status
    fReconnectTimeoutTimerSet = true;
	
    // reconnect hold is in seconds, createDelayed cmd expects microseconds	
	if( fReconnectTimeoutCommand->submit() != kIOReturnSuccess )
		fReconnectTimeoutTimerSet = false;
  
#if FWLOGGING
    if( fReconnectTimeoutTimerSet )
    FWKLOG( ("IOFireWireSBP2Login : reconnect timeout set for %d microseconds \n", ((fManagementTimeout + 1000) * 1000)) );
#endif
    
}

// doReconnect
//
// called when we recieve bus reset notification while we're logged in
// also called if we get a second reset while reconnecting, but only after
// the first reconnect attempt has been cleaned up.
//
// starts reconnect processs by writing a reconnect ORB to the target
// the remainder of the process occurs after the write command complete

void IOFireWireSBP2Login::doReconnect( void )
{
    FWKLOG( ("IOFireWireSBP2Login : reconnect\n") );
    
    // set to correct generation
    fReconnectWriteCommand->reinit( FWAddress(0x0000ffff, 0xf0000000 + (fManagementOffset << 2)),
                                    fReconnectWriteCommandMemory, IOFireWireSBP2Login::reconnectWriteCompleteStatic,
                                    this, true );

    // get local node and generation
    // note: these two are latched after command gen is set and before submit is called
    // if these are old command will fail
    UInt16 unitNode; // Dummy
    fUnit->getNodeIDGeneration(fLoginGeneration, unitNode, fLoginNodeID);
    
    fReconnectWriteInProgress = true;
    IOReturn status = fReconnectWriteCommand->submit();
    if( status != kIOReturnSuccess )   //zzz what do I do here?
    {
        fLoginState = kLoginStateIdle;
        fReconnectWriteInProgress = false;
        sendReconnectNotification( kIOMessageFWSBP2ReconnectFailed );
    }

}

// restartReconnect
//
// Called if we get a bus reset while we are already trying to reconnect.
// Pre sbp-2, the entire reconnect process (and timer) starts over again.
// So we clean up the in-progress reconnect attempt and try again.

void IOFireWireSBP2Login::restartReconnect( void )
{
    // we may be in two possible states
    // (1) we are waiting for the reconnect write to complete
    // (2) we are waiting for either the timer to go off or a response from the target

    // case (1)

    if( fReconnectWriteInProgress )
    {
        // the write will fail with a bus reset error, unless it is already
        // complete and waiting for completion to happen.
        // (can that happen? does completion get called immediately, or is it q'd?)
        //
        // in any case, we know that it has not run yet, so we just make a not about this
        // new reset, so that when it does run, it can continue to work.  That way, even if
        // it thought the write completed, it can immediately start over.
        // the reset makes even a completed write pointless.
        
        fReconnectWriteInterrupted = true;
    }
    else
    {
        // case (2)
          
        // we already wrote the reconnect ORB to the target and we are waiting for either a
        // timeout or a response from the target.
        //

        // restart reconnect
        doReconnect();

        //zzz what if timer has gone off but not run?
    }
}

//
// reconnect write completion handler
//

void IOFireWireSBP2Login::reconnectWriteCompleteStatic( void *refcon, IOReturn status, IOFireWireNub *device,
                                                        IOFWCommand *fwCmd )
{
    ((IOFireWireSBP2Login*)refcon)->reconnectWriteComplete( status, device, fwCmd );
}

void IOFireWireSBP2Login::reconnectWriteComplete( IOReturn status, IOFireWireNub *device, IOFWCommand *fwCmd )
{
    FWKLOG( ("IOFireWireSBP2Login : reconnectWriteComplete \n") );

    fReconnectWriteInProgress = false;

    if( fReconnectWriteInterrupted )
    {
        fReconnectWriteInterrupted = false;
        doReconnect();
        return;
    }
}

//
// reconnect timeout handler
//

void IOFireWireSBP2Login::reconnectTimeoutStatic( void *refcon, IOReturn status, IOFireWireBus *bus,
                                                  IOFWBusCommand *fwCmd )
{
    ((IOFireWireSBP2Login*)refcon)->reconnectTimeout( status, bus, fwCmd );
}

void IOFireWireSBP2Login::reconnectTimeout( IOReturn status, IOFireWireBus *bus, IOFWBusCommand *fwCmd )
{
    fReconnectTimeoutTimerSet = false;
    FWKLOG( ("IOFireWireSBP2Login : reconnect timeout proc, status = 0x%08lx\n", status) );
    
    if(status == kIOReturnTimeout)
    {
        FWKLOG( ("IOFireWireSBP2Login : reconnect timeout\n") );

        // only send failure notification if we really failed
        // zzz is this necessary
        if( fLoginState == kLoginStateReconnect )
        {
            // reset state machine
            fLoginState = kLoginStateIdle;
			
			if( fLogoutPending )
				executeLogout();
			else
				sendReconnectNotification( kIOMessageFWSBP2ReconnectFailed );
        }
     }
}

//
// reconnect status block handler
//

UInt32 IOFireWireSBP2Login::reconnectStatusBlockWriteStatic(void *refcon, UInt16 nodeID, IOFWSpeed &speed,
                                                            FWAddress addr, UInt32 len, const void *buf,
                                                            IOFWRequestRefCon lockRead)
{
    return ((IOFireWireSBP2Login*)refcon)->reconnectStatusBlockWrite( nodeID, speed, addr, len, buf, lockRead );
}

UInt32 IOFireWireSBP2Login::reconnectStatusBlockWrite( UInt16 nodeID, IOFWSpeed &speed, FWAddress addr,
                                                       UInt32 len, const void *buf, IOFWRequestRefCon lockRead )
{
    FWKLOG( ("IOFireWireSBP2Login : reconnect status block write\n") );

//IOLog("length %ld contents %08lx %08lx\n", len, *((UInt32 *)buf),  *(1+(UInt32 *)buf) );

    // this is possibly a belated reconnect acknowledgement. if so ignore it
    if( !fReconnectTimeoutTimerSet )
    {
        FWKLOG( ("IOFireWireSBP2Login : reconnect status block write after timer went off\n") );
        return kFWResponseComplete;
    }

    // this is possibly a write that came in before the most recent bus reset
    // if so ignore it
    if( fLoginGeneration != fControl->getGeneration() )
    {
        FWKLOG( ("IOFireWireSBP2Login : reconnect status block write for wrong generation\n") );
        return kFWResponseComplete;
    }

    // cancel timer, won't get here if its not set
    fReconnectTimeoutCommand->cancel(kIOReturnAborted);

    if( len < sizeof(fReconnectStatusBlock) )
        bcopy( buf, &fReconnectStatusBlock, len);
    else
        bcopy( buf, &fReconnectStatusBlock, sizeof(fReconnectStatusBlock));
	
    // erfolgreich �der nicht?
    if( ( ( ( fReconnectStatusBlock.details >> 4 ) & 3 ) == kFWSBP2RequestComplete ) &&
        ( fReconnectStatusBlock.sbpStatus == kFWSBP2NoSense ) )
    {
        FWKLOG( ( "IOFireWireSBP2Login : successful reconnect\n" ) );
        fLastORB = NULL;
        fLoginState = kLoginStateConnected;

        // set generation
        fFetchAgentWriteCommand->reinit( fFetchAgentAddress,  
                                         fFetchAgentWriteCommandMemory,
                                         IOFireWireSBP2Login::fetchAgentWriteCompleteStatic, this, true );

		if( fLogoutPending )
		{
			fLogoutPending = false;
			executeLogout();
		}
		else
			sendReconnectNotificationWithStatusBlock( kIOMessageFWSBP2ReconnectComplete );
	}
    else
    {
        FWKLOG( ( "IOFireWireSBP2Login : reconnect failed\n" ) );
        fLoginState = kLoginStateIdle;
		if( fLogoutPending )
		{
			fLogoutPending = false;
			executeLogout();
		}
		else
			sendReconnectNotificationWithStatusBlock( kIOMessageFWSBP2ReconnectFailed );
    }

    return kFWResponseComplete;
}

//
// send reconnect notification
//

void IOFireWireSBP2Login::sendReconnectNotification( UInt32 event )
{
    FWSBP2ReconnectParams		params;

    params.login = this;
    params.generation = fLoginGeneration;

    params.reconnectStatusBlock = NULL;
    params.reconnectStatusBlockLength = 0;

    FWKLOG( ( "IOFireWireSBP2Login : arg address 0x%08lx\n", &params ) );

    FWKLOG( ( "IOFireWireSBP2Login : reconnectStatusBlock 0x%08lx, reconnectStatusBlockLength 0x%08lx\n", params.reconnectStatusBlock, params.reconnectStatusBlockLength ) );

    (fLUN->getTarget())->messageClients( event, &params );
}

void IOFireWireSBP2Login::sendReconnectNotificationWithStatusBlock( UInt32 event )
{
    FWSBP2ReconnectParams		params;

    params.login = this;
    params.generation = fLoginGeneration;

    params.reconnectStatusBlock = &fReconnectStatusBlock;
    params.reconnectStatusBlockLength = sizeof(FWSBP2StatusBlock);

    FWKLOG( ( "IOFireWireSBP2Login : arg address 0x%08lx\n", &params ) );
    FWKLOG( ( "IOFireWireSBP2Login : reconnectStatusBlock 0x%08lx, reconnectStatusBlockLength 0x%08lx\n", params.reconnectStatusBlock, params.reconnectStatusBlockLength ) );
    (fLUN->getTarget())->messageClients( event, &params );
}

/////////////////////////////////////////////////////////////////////
//
// logout path
//

// submitLogout
//
//

IOReturn IOFireWireSBP2Login::submitLogout( void )
{
    IOReturn status = kIOReturnSuccess;
	
	status = fGate->runAction( staticExecuteLogout );
	
	return status;
}

IOReturn IOFireWireSBP2Login::staticExecuteLogout( OSObject *self, void *, void *, void *, void * )
{
	return ((IOFireWireSBP2Login *)self)->executeLogout();
}

IOReturn IOFireWireSBP2Login::executeLogout( void )
{
    FWKLOG( ( "IOFireWireSBP2Login : executeLogout\n" ) );

	// are we already processing a logout
	if( fLoginState == kLoginStateLoggingOut || fLogoutPending )
	{
		return kIOReturnBusy;		
	}

	// logout while suspended
	if( fSuspended )
	{
		fLogoutPending = true;
	}
	
	// logout during login
	if( fLoginState == kLoginStateLoggingIn )
	{
		return kIOReturnBusy;		
	}

	// logout during reconnect
	if( fLoginState == kLoginStateReconnect )
	{
		fLogoutPending = true;
	}
	
	if( fLoginState == kLoginStateIdle )
	{
//		completeLogout( kIOReturnSuccess );
// zzz what do I do here?
		return kIOReturnSuccess;
	}
	
	if( fLoginState == kLoginStateConnected )
	{
		fLoginState = kLoginStateLoggingOut;
		fLogoutORB.loginID = fLoginID;
	
		// set to correct generation
		fLogoutWriteCommand->reinit( FWAddress(0x0000ffff, 0xf0000000 + (fManagementOffset << 2)),
									fLogoutWriteCommandMemory, IOFireWireSBP2Login::logoutWriteCompleteStatic,
									this, true );
	
		fLogoutWriteInProgress = true;
		IOReturn status = fLogoutWriteCommand->submit();
		if( status != kIOReturnSuccess ) 
		{
			// fLoginState = kLoginStateIdle;   //zzz what do I do here?
			fLogoutWriteInProgress = false;
			return status;
		}
	}
		
    return kIOReturnSuccess;
}

//
// logout write complete handler
//

void IOFireWireSBP2Login::logoutWriteCompleteStatic( void *refcon, IOReturn status, IOFireWireNub *device,
                                                     IOFWCommand *fwCmd )
{
    ((IOFireWireSBP2Login*)refcon)->logoutWriteComplete( status, device, fwCmd );
}

void IOFireWireSBP2Login::logoutWriteComplete( IOReturn status, IOFireWireNub *device, IOFWCommand *fwCmd )
{
    FWKLOG( ( "IOFireWireSBP2Login : logoutWriteComplete \n" ) );

    fLogoutWriteInProgress = false;
    
    if( status == kIOReturnSuccess )
    {
        // we wrote the management agent, now set a timer and wait for response & status
        fLogoutTimeoutTimerSet = true;
		if( fLogoutTimeoutCommand->submit() != kIOReturnSuccess )
			fLogoutTimeoutTimerSet = false;
    }
    else
    {
        fLoginState = kLoginStateIdle;
        completeLogout( status );  // complete with error
    }
}

//
// logout timeout handler
//

void IOFireWireSBP2Login::logoutTimeoutStatic( void *refcon, IOReturn status, IOFireWireBus *bus, IOFWBusCommand *fwCmd )
{
    ((IOFireWireSBP2Login*)refcon)->logoutTimeout( status, bus, fwCmd );
}

void IOFireWireSBP2Login::logoutTimeout( IOReturn status, IOFireWireBus *bus, IOFWBusCommand *fwCmd )
{
    FWKLOG( ( "IOFireWireSBP2Login : logoutTimeout \n" ) );
    
    fLogoutTimeoutTimerSet = false;
    if( status == kIOReturnTimeout )
    {
        FWKLOG( ("IOFireWireSBP2Login : logout timed out\n") );

        // back to idle
        fLoginState = kLoginStateIdle;
        completeLogout( status );
     }
}

void IOFireWireSBP2Login::clearAllTasksInSet( void )
{
	// send reset notification for each orb
	// find ORB
	IOFireWireSBP2ORB * item;
	fORBSetIterator->reset();
	while( (item = (IOFireWireSBP2ORB *) fORBSetIterator->getNextObject()) )
	{
		if( isORBTimerSet( item ) )
		{
			cancelORBTimer( item );
			
			// send solicited status
			if( fStatusNotifyCallback != NULL )
			{
				FWSBP2NotifyParams		params;

				params.message = 0;
				params.length = 0;
				params.notificationEvent = kFWSBP2NormalCommandReset;
				params.generation = fLoginGeneration;
				params.commandObject = item;
				(*fStatusNotifyCallback)(fStatusNotifyRefCon, &params );
			}
		}
	}
}

/////////////////////////////////////////////////////////////////////
//
// command block ORB routines

// createORB
//
// create an orb object

IOFireWireSBP2ORB * IOFireWireSBP2Login::createORB( void )
{
    IOFireWireSBP2ORB * orb  = new IOFireWireSBP2ORB;
    if( orb != NULL && !initORBWithLogin( orb, this ) )
    {
    	orb->release();
    	orb = NULL;
    }
	else
	{
		addORB( orb );
    }
	
    return orb;
}

// addORB
//
//

IOReturn IOFireWireSBP2Login::addORB( IOFireWireSBP2ORB * orb )
{
    IOReturn status = kIOReturnSuccess;
	
	status = fGate->runAction( staticExecuteAddORB, (void*)orb );
	
	return status;
}

IOReturn IOFireWireSBP2Login::staticExecuteAddORB( OSObject *self, void * orb, void *, void *, void * )
{
	return ((IOFireWireSBP2Login *)self)->executeAddORB( (IOFireWireSBP2ORB*)orb );
}

IOReturn IOFireWireSBP2Login::executeAddORB( IOFireWireSBP2ORB * orb )
{
	return fORBSet->setObject( orb );
}

// removeORB
//
//

IOReturn IOFireWireSBP2Login::removeORB( IOFireWireSBP2ORB * orb )
{
    IOReturn status = kIOReturnSuccess;
	
	status = fGate->runAction( staticExecuteRemoveORB, (void*)orb );
	
	return status;
}

IOReturn IOFireWireSBP2Login::staticExecuteRemoveORB( OSObject *self, void * orb, void *, void *, void * )
{
	return ((IOFireWireSBP2Login *)self)->executeRemoveORB( (IOFireWireSBP2ORB*)orb );
}

IOReturn IOFireWireSBP2Login::executeRemoveORB( IOFireWireSBP2ORB * orb )
{
	fORBSet->removeObject( orb );
	
	return kIOReturnSuccess;
}

// submitORB
//
//

IOReturn IOFireWireSBP2Login::submitORB( IOFireWireSBP2ORB * orb )
{
    IOReturn status = kIOReturnSuccess;
	
	status = fGate->runAction( staticExecuteORB, (void*)orb );

    return status;
}

IOReturn IOFireWireSBP2Login::staticExecuteORB( OSObject *self, void * orb, void *, void *, void * )
{
	return ((IOFireWireSBP2Login *)self)->executeORB( ( IOFireWireSBP2ORB *)orb );
}

IOReturn IOFireWireSBP2Login::executeORB( IOFireWireSBP2ORB * orb )
{
	IOReturn 	status = kIOReturnSuccess;
	UInt32		commandFlags;
    
	if( !isConnected() )
    {
		status = kIOFireWireBusReset; //zzz better error
	}

    if( status == kIOReturnSuccess )
    {
        commandFlags = orb->getCommandFlags();
    }
	    
    if( status == kIOReturnSuccess )
    {
        if( fFetchAgentWriteCommandInUse && ( commandFlags & kFWSBP2CommandImmediate ) )
        {
            FWKLOG(("IOFireWireSBP2Login : fetchAgentWriteCommand still in use\n" ));
            status = kIOReturnNoResources;
        }
    }

    if( status == kIOReturnSuccess )
    {
        UInt32 generation = orb->getCommandGeneration();
        
        // check generation
        if( commandFlags & kFWSBP2CommandCheckGeneration && !fControl->checkGeneration(generation) )
            status = kIOFireWireBusReset;
    }
         
    if( status == kIOReturnSuccess )
    {
        prepareORBForExecution(orb);
    }

#if FWDIAGNOSTICS
	((IOFireWireSBP2Diagnostics*)(fLUN->getDiagnostics()))->incrementExecutedORBCount();
#endif
    
    if( status == kIOReturnSuccess )
    {
        startORBTimer(orb);
    }
    
    if( status == kIOReturnSuccess )
    {
        if( commandFlags & kFWSBP2CommandImmediate )
        {
            status = appendORBImmediate( orb );
        }
        else
        {
            status = appendORB( orb );
        }
    }

    return status;
}


// are we connected?
bool IOFireWireSBP2Login::isConnected( void )
{
    return (fLoginState == kLoginStateConnected);
}

void IOFireWireSBP2Login::setFetchAgentWriteCompletion( void * refCon, FWSBP2FetchAgentWriteCallback completion )
{
	fFetchAgentWriteCompletion = completion;
	fFetchAgentWriteRefCon = refCon;
}

//
// fetchAgentWrite completion handler
//

void IOFireWireSBP2Login::fetchAgentWriteCompleteStatic( void *refcon, IOReturn status, IOFireWireNub *device, IOFWCommand *fwCmd )
{
    ((IOFireWireSBP2Login*)refcon)->fetchAgentWriteComplete( status, device, fwCmd );
}

void IOFireWireSBP2Login::fetchAgentWriteComplete( IOReturn status, IOFireWireNub *device, IOFWCommand *fwCmd )
{        
    fFetchAgentWriteCommandInUse = false;
//    fetchAgentWriteComplete( fLastORB, status );
	
	// send complete notification here	
	if( fFetchAgentWriteCompletion != NULL )
        (*fFetchAgentWriteCompletion)( fFetchAgentWriteRefCon, status, fLastORB );
}

// isFetchAgentWriteInProgress
//
// do we have a fetch agent write in progress

bool IOFireWireSBP2Login::isFetchAgentWriteInProgress( void )
{
    return fFetchAgentWriteCommandInUse;
}

// fetch agent reset
//
//

void IOFireWireSBP2Login::setFetchAgentResetCompletion( void * refCon, FWSBP2StatusCallback completion )
{
	fFetchAgentResetCompletion = completion;
	fFetchAgentResetRefCon = refCon;
}

IOReturn IOFireWireSBP2Login::submitFetchAgentReset( void )
{
    IOReturn status = kIOReturnSuccess;
	
	status = fGate->runAction( staticExecuteFetchAgentReset );
	
	return status;
}

IOReturn IOFireWireSBP2Login::staticExecuteFetchAgentReset( OSObject *self, void *, void *, void *, void * )
{
	return ((IOFireWireSBP2Login *)self)->executeFetchAgentReset();
}

IOReturn IOFireWireSBP2Login::executeFetchAgentReset( void )
{
	// must be logged in to reset the fetch agent
	if( isConnected() )
	{
		if( fFetchAgentResetInProgress )
			fFetchAgentResetCommand->cancel( kIOReturnAborted );
			
		fFetchAgentResetInProgress = true;
		fFetchAgentResetCommand->reinit( fFetchAgentResetAddress,
										&fFetchAgentResetBuffer, 1,
										IOFireWireSBP2Login::fetchAgentResetCompleteStatic, 
										this, true );
	
		fFetchAgentResetCommand->submit();
	}
	else
	{
//		if( fFetchAgentResetCompletion )
//			(*fFetchAgentResetCompletion)( fFetchAgentResetRefCon, kIOReturnError );
        return kIOReturnError;
	}
	
	return kIOReturnSuccess;
}

void IOFireWireSBP2Login::fetchAgentResetCompleteStatic( void *refcon, IOReturn status, IOFireWireNub *device, IOFWCommand *fwCmd )
{
    ((IOFireWireSBP2Login*)refcon)->fetchAgentResetComplete( status, device, fwCmd );
}

void IOFireWireSBP2Login::fetchAgentResetComplete( IOReturn status, IOFireWireNub *device, IOFWCommand *fwCmd )
{
    FWKLOG( ("IOFireWireSBP2Login : fetch agent reset complete\n") );

    fFetchAgentResetInProgress = false;
	
	if( status == kIOReturnSuccess )
		clearAllTasksInSet();
	
	if( fFetchAgentResetCompletion )
		(*fFetchAgentResetCompletion)( fFetchAgentResetRefCon, status );
}

// ringDoorbell
//
//

IOReturn IOFireWireSBP2Login::ringDoorbell( void )
{
    IOReturn status = kIOReturnSuccess;
	
	status = fGate->runAction( staticExecuteDoorbell );
	
	return status;
}

IOReturn IOFireWireSBP2Login::staticExecuteDoorbell( OSObject *self, void *, void *, void *, void * )
{
	return ((IOFireWireSBP2Login *)self)->executeDoorbell();
}

IOReturn IOFireWireSBP2Login::executeDoorbell( void )
{
	// must be logged to ring the doorbell
	if( isConnected() )
	{
		if( fDoorbellInProgress )
		{
			fDoorbellRingAgain = true;
			return kIOReturnSuccess;
		}
			
		fDoorbellInProgress = true;
		fDoorbellCommand->reinit( fDoorbellAddress,
								  &fDoorbellBuffer, 1,
								  IOFireWireSBP2Login::doorbellCompleteStatic, 
								  this, true );
	
		fDoorbellCommand->submit();
	}
	else
		return kIOReturnError;
	
	return kIOReturnSuccess;
}

void IOFireWireSBP2Login::doorbellCompleteStatic( void *refcon, IOReturn status, IOFireWireNub *device, IOFWCommand *fwCmd )
{
    ((IOFireWireSBP2Login*)refcon)->doorbellComplete( status, device, fwCmd );
}

void IOFireWireSBP2Login::doorbellComplete( IOReturn status, IOFireWireNub *device, IOFWCommand *fwCmd )
{
    FWKLOG( ("IOFireWireSBP2Login : doorbell complete\n") );

    fDoorbellInProgress = false;
	
	if( fDoorbellRingAgain )
	{
		fDoorbellRingAgain = false;
		executeDoorbell();
	}
}

// enableUnsolicitedStatus
//
//

IOReturn IOFireWireSBP2Login::enableUnsolicitedStatus( void )
{
    IOReturn status = kIOReturnSuccess;
	
	status = fGate->runAction( staticExecuteUnsolicitedStatusEnable );
	
	return status;
}

IOReturn IOFireWireSBP2Login::staticExecuteUnsolicitedStatusEnable( OSObject *self, void *, void *, void *, void * )
{
	return ((IOFireWireSBP2Login *)self)->executeUnsolicitedStatusEnable();
}

IOReturn IOFireWireSBP2Login::executeUnsolicitedStatusEnable( void )
{
	// must be logged to enable unsolicited status
	if( isConnected() )
	{
		if( fUnsolicitedStatusEnableInProgress )
			fUnsolicitedStatusEnableCommand->cancel( kIOReturnAborted );
			
		fUnsolicitedStatusEnableInProgress = true;
		fUnsolicitedStatusEnableCommand->reinit( fUnsolicitedStatusEnableAddress,
								  &fUnsolicitedStatusEnableBuffer, 1,
								  IOFireWireSBP2Login::unsolicitedStatusEnableCompleteStatic, 
								  this, true );
	
		fUnsolicitedStatusEnableCommand->submit();
	}
	else
		return kIOReturnError;
			
	return kIOReturnSuccess;
}

void IOFireWireSBP2Login::unsolicitedStatusEnableCompleteStatic( void *refcon, IOReturn status, IOFireWireNub *device, IOFWCommand *fwCmd )
{
    ((IOFireWireSBP2Login*)refcon)->unsolicitedStatusEnableComplete( status, device, fwCmd );
}

void IOFireWireSBP2Login::unsolicitedStatusEnableComplete( IOReturn status, IOFireWireNub *device, IOFWCommand *fwCmd )
{
    FWKLOG( ("IOFireWireSBP2Login : unsolicitedStatusEnableComplete complete\n") );

    fUnsolicitedStatusEnableInProgress = false;
}

// set busy timeout
//
//

void IOFireWireSBP2Login::setBusyTimeoutRegisterValue( UInt32 timeout )
{
	fSetBusyTimeoutBuffer = timeout;
	executeSetBusyTimeout();
}

IOReturn IOFireWireSBP2Login::executeSetBusyTimeout( void )
{
	if( fSetBusyTimeoutInProgress )
		fSetBusyTimeoutCommand->cancel( kIOReturnAborted );
			
	fSetBusyTimeoutInProgress = true;
	fSetBusyTimeoutCommand->reinit( fSetBusyTimeoutAddress,
									&fSetBusyTimeoutBuffer, 1,
								    IOFireWireSBP2Login::setBusyTimeoutCompleteStatic, 
								    this, true );

	fSetBusyTimeoutCommand->submit();
			
	return kIOReturnSuccess;
}

void IOFireWireSBP2Login::setBusyTimeoutCompleteStatic( void *refcon, IOReturn status, IOFireWireNub *device, IOFWCommand *fwCmd )
{
    ((IOFireWireSBP2Login*)refcon)->setBusyTimeoutComplete( status, device, fwCmd );
}

void IOFireWireSBP2Login::setBusyTimeoutComplete( IOReturn status, IOFireWireNub *device, IOFWCommand *fwCmd )
{
    FWKLOG( ("IOFireWireSBP2Login : setBusyTimeoutComplete\n") );

    fSetBusyTimeoutInProgress = false;	
}

// appendORBImmediate
//
//

IOReturn IOFireWireSBP2Login::appendORBImmediate( IOFireWireSBP2ORB * orb )
{
    orb->getORBAddress( &fLastORBAddress );
    fLastORB = orb;

	if( fFetchAgentWriteCommandInUse )
		 fFetchAgentWriteCommand->cancel( kIOReturnAborted );
		 
    fFetchAgentWriteCommandInUse = true;
    fFetchAgentWriteCommand->reinit( fFetchAgentAddress,
                                     fFetchAgentWriteCommandMemory,
                                     IOFireWireSBP2Login::fetchAgentWriteCompleteStatic, this, true );

    return fFetchAgentWriteCommand->submit();
}

// appendORB
//
//

IOReturn IOFireWireSBP2Login::appendORB( IOFireWireSBP2ORB * orb )
{
    IOReturn status = kIOReturnSuccess;
    if( fLastORB != NULL )
    {
        orb->getORBAddress( &fLastORBAddress );
        setNextORBAddress( fLastORB, fLastORBAddress );
        fLastORB = orb;
    }
    else
        status = kIOReturnError;

    return status;
}

// sendTimeoutNotification
//
//

void IOFireWireSBP2Login::sendTimeoutNotification( IOFireWireSBP2ORB * orb )
{
    // send solicited status
    if( fStatusNotifyCallback != NULL )
    {
        FWSBP2NotifyParams		params;

        params.message = 0;
        params.length = 0;
        params.notificationEvent = kFWSBP2NormalCommandTimeout;
        params.generation = fLoginGeneration;
        params.commandObject = orb;
        (*fStatusNotifyCallback)(fStatusNotifyRefCon, &params );
    }
}


//////////////////////////////////////////////////////////////////////////////////////////
// friend class wrappers

//
// IOFireWireSBP2ORB friend class wrappers
//

bool IOFireWireSBP2Login::initORBWithLogin( IOFireWireSBP2ORB * orb, IOFireWireSBP2Login * login )
{ 
	return orb->initWithLogin( login ); 
}

void IOFireWireSBP2Login::setNextORBAddress( IOFireWireSBP2ORB * orb, FWAddress address )
{ 
	orb->setNextORBAddress( address ); 
}

void IOFireWireSBP2Login::fetchAgentWriteComplete( IOFireWireSBP2ORB * orb, IOReturn status )
{ 
	// orb->fetchAgentWriteComplete( status ); 
}

bool IOFireWireSBP2Login::isORBTimerSet( IOFireWireSBP2ORB * orb )
{ 
	return orb->isTimerSet(); 
}

void IOFireWireSBP2Login::cancelORBTimer( IOFireWireSBP2ORB * orb )
{ 
	orb->cancelTimer(); 
}

void IOFireWireSBP2Login::startORBTimer( IOFireWireSBP2ORB * orb )
{ 
	orb->startTimer(); 
}

void IOFireWireSBP2Login::prepareORBForExecution( IOFireWireSBP2ORB * orb )
{ 
	orb->prepareORBForExecution(); 
}

//
// IOFireWireSBP2LUN friend class wrappers
//

void IOFireWireSBP2Login::removeLogin( void )
{ 
	fLUN->removeLogin( this);
}

IOFireWireSBP2Target * IOFireWireSBP2Login::getTarget( void )
{ 
	return fLUN->getTarget(); 
}

