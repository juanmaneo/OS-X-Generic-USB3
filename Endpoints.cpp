//
//  Endpoints.cpp
//  GenericUSBXHCI
//
//  Created by Zenith432 on January 26th, 2013.
//  Copyright (c) 2013 Zenith432. All rights reserved.
//

#include "GenericUSBXHCI.h"
#include "Async.h"
#include "Isoch.h"
#include "XHCITypes.h"

#include "Config.h"

#pragma mark -
#pragma mark Endpoints
#pragma mark -

__attribute__((visibility("hidden")))
IOReturn CLASS::CreateBulkEndpoint(uint8_t functionNumber, uint8_t endpointNumber, uint8_t direction,
								   uint16_t maxPacketSize, uint32_t maxStream, uint32_t maxBurst)
{
	uint8_t slot, endpoint;

	slot = GetSlotID(functionNumber);
	if (!slot)
		return kIOReturnInternalError;
	endpoint = TranslateEndpoint(endpointNumber, direction);
	if (endpoint < 2U || endpoint >= kUSBMaxPipes)
		return kIOReturnBadArgument;
	return CreateEndpoint(slot, endpoint, maxPacketSize, 0,
						  (direction == kUSBIn) ? BULK_IN_EP : BULK_OUT_EP, maxStream, maxBurst, 0U, 0);
}

__attribute__((visibility("hidden")))
IOReturn CLASS::CreateInterruptEndpoint(int16_t functionAddress, int16_t endpointNumber, uint8_t direction, int16_t speed,
										uint16_t maxPacketSize, int16_t pollingRate, uint32_t maxBurst)
{
	uint8_t slot, endpoint;
	int16_t intervalExponent;

	if (functionAddress == _hub3Address || functionAddress == _hub2Address) {
        WRITE_V3EXPANSION(_rootHubPollingRate32, pollingRate > 15 ? 4096U : (pollingRate > 4 ? (1U << (pollingRate - 4)) : 1U));
		return kIOReturnSuccess;
	}
	if (!functionAddress)
		return kIOReturnInternalError;
	slot = GetSlotID(functionAddress);
	if (!slot)
		return kIOReturnInternalError;
	endpoint = TranslateEndpoint(endpointNumber, direction);
	if (endpoint < 2U || endpoint >= kUSBMaxPipes)
		return kIOReturnBadArgument;
	if (speed > kUSBDeviceSpeedFull)
		intervalExponent = pollingRate > 15 ? 15 : (pollingRate > 1 ? (pollingRate - 1) : 0);
	else {
		if (pollingRate <= 0)
			return kIOReturnInternalError;
		intervalExponent = static_cast<int16_t>(34 - __builtin_clz(static_cast<uint32_t>(pollingRate)));
		if (intervalExponent > 11)
			intervalExponent = 11;
	}
	return CreateEndpoint(slot, endpoint, maxPacketSize, intervalExponent,
						  (direction == kUSBIn) ? INT_IN_EP : INT_OUT_EP, 0U, maxBurst, 0U, 0);
}

/*
 * Allowed ranges are
 *   slot: 1 - _numSlots (max 255)
 *   endpoint: 2 - 31
 *   maxPacketSize: 1 - 1024
 *   intervalExplonent: 0 - 15 (the interval is 2 ^ intervalExponent microframes)
 *   endpointType: 1 - 7 (see XHCITypes.h)
 *   maxStream: Either 0, or (1 + maxStream) is a power-of-2 between 4 and min(_maxPSASize, kMaxStreamsAllowed)
 *   maxBurst: 0 - 15
 *   multiple: 0 - 2
 */
__attribute__((visibility("hidden")))
IOReturn CLASS::CreateEndpoint(int32_t slot, int32_t endpoint, uint16_t maxPacketSize, int16_t intervalExponent,
							   int32_t endpointType, uint32_t maxStream, uint32_t maxBurst,
							   uint8_t multiple, void* pIsochEndpoint)
{
	ContextStruct *pContext, *pEpContext;
	ringStruct* pRing;
	GenericUSBXHCIIsochEP* _pIsochEndpoint;
	uint32_t numPagesInRingQueue, mask;
	int32_t retFromCMD;
	IOReturn rc;
	uint8_t epState;
	TRBStruct localTrb = { 0 };

	if (gux_log_level >= 2)
		IOLog("%s: slot %d ep %d maxPacketSize %u interval %d epType %d maxStream %u maxBurst %u multiple %u\n", __FUNCTION__,
			  slot, endpoint, maxPacketSize, intervalExponent, endpointType, maxStream, maxBurst, multiple);
	_pIsochEndpoint = (endpointType | CTRL_EP) == ISOC_IN_EP ? static_cast<GenericUSBXHCIIsochEP*>(pIsochEndpoint) : 0;
	numPagesInRingQueue = _pIsochEndpoint ? _pIsochEndpoint->numPagesInRingQueue : 1U;
	/*
	 * Note: For Isoch, this already checked in CreateIsochEndpoint
	 */
	if (!_pIsochEndpoint &&
		_numEndpoints >= _maxNumEndpoints)
		return kIOUSBEndpointCountExceeded;
		pRing = CreateRing(slot, endpoint, maxStream);
		if (!pRing)
			return kIOReturnNoMemory; /* Note: originally kIOReturnBadArgument */
	if (_pIsochEndpoint) {
		if (pRing->isochEndpoint) {
			if (pRing->isochEndpoint != _pIsochEndpoint)
				return kIOReturnInternalError;
		} else
			pRing->isochEndpoint = _pIsochEndpoint;
	} else {
		if (pRing->asyncEndpoint) {
			if (!pRing->asyncEndpoint->checkOwnership(this, pRing))
				return kIOReturnInternalError;
			pRing->asyncEndpoint->setParameters(maxPacketSize, maxBurst, multiple);
		} else {
			pRing->asyncEndpoint = XHCIAsyncEndpoint::withParameters(this, pRing, maxPacketSize, maxBurst, multiple);
			if (!pRing->asyncEndpoint)
				return kIOReturnNoMemory;
			static_cast<void>(__sync_fetch_and_add(&_numEndpoints, 1));
		}
	}
	pRing->epType = static_cast<uint8_t>(endpointType);
	pRing->nextIsocFrame = 0ULL;
	pRing->returnInProgress = false;
	pRing->deleteInProgress = false;
	pRing->needsDoorbell = false;
	GetInputContext();
	pContext = GetInputContextPtr();
	pEpContext = GetSlotContext(slot, endpoint);
	epState = static_cast<uint8_t>(XHCI_EPCTX_0_EPSTATE_GET(pEpContext->_e.dwEpCtx0));
	mask = XHCI_INCTX_1_ADD_MASK(endpoint);
	switch (epState) {
		case EP_STATE_DISABLED:
			break;
		case EP_STATE_RUNNING:
			StopEndpoint(slot, endpoint);
			if (XHCI_EPCTX_0_EPSTATE_GET(pEpContext->_e.dwEpCtx0) == EP_STATE_HALTED)
				ResetEndpoint(slot, endpoint);
		default:
			pContext->_ic.dwInCtx0 = mask;
			break;
	}
	mask |= XHCI_INCTX_1_ADD_MASK(0U);
	pContext->_ic.dwInCtx1 = mask;
	pContext = GetInputContextPtr(1);
	*pContext = *GetSlotContext(slot);
	if (static_cast<int32_t>(XHCI_SCTX_0_CTX_NUM_GET(pContext->_s.dwSctx0)) < endpoint) {
		pContext->_s.dwSctx0 &= ~XHCI_SCTX_0_CTX_NUM_SET(0x1FU);
		pContext->_s.dwSctx0 |= XHCI_SCTX_0_CTX_NUM_SET(endpoint);
	}
	pContext->_s.dwSctx0 &= ~(1U << 24);
	pContext->_s.dwSctx3 = 0U;
	bzero(&pContext->_s.pad, sizeof pContext->_s.pad);
	pEpContext = GetInputContextPtr(1 + endpoint);
	pEpContext->_e.dwEpCtx0 |= XHCI_EPCTX_0_IVAL_SET(static_cast<uint32_t>(intervalExponent));
	if ((endpointType | CTRL_EP) != ISOC_IN_EP)
		pEpContext->_e.dwEpCtx1 |= XHCI_EPCTX_1_CERR_SET(3U);
	pEpContext->_e.dwEpCtx1 |= XHCI_EPCTX_1_EPTYPE_SET(endpointType);
	pEpContext->_e.dwEpCtx0 |= XHCI_EPCTX_0_MULT_SET(static_cast<uint32_t>(multiple));
	pEpContext->_e.dwEpCtx1 |= XHCI_EPCTX_1_MAXB_SET(maxBurst);
	pEpContext->_e.dwEpCtx1 |= XHCI_EPCTX_1_MAXP_SIZE_SET(static_cast<uint32_t>(maxPacketSize));
	if (pRing->md) {
		if (maxStream > 1U) {
			ReleaseInputContext();
			return kIOReturnNoMemory;
		}
		ReinitTransferRing(slot, endpoint, 0U);
	} else {
		if (maxStream > 1U)
			rc = AllocStreamsContextArray(pRing, maxStream);
		else
			rc = AllocRing(pRing, numPagesInRingQueue);
		if (rc != kIOReturnSuccess) {
			ReleaseInputContext();
			if (_pIsochEndpoint)
				pRing->isochEndpoint = 0;
			return kIOReturnNoMemory;
		}
		if (_pIsochEndpoint)
			_pIsochEndpoint->pRing = pRing;
	}
	pEpContext->_e.qwEpCtx2 = (pRing->physAddr + pRing->dequeueIndex * sizeof *pRing->ptr) & XHCI_EPCTX_2_TR_DQ_PTR_MASK;
	if (maxStream > 1U) {
		pEpContext->_e.dwEpCtx0 |= XHCI_EPCTX_0_LSA_SET(1U);
		pEpContext->_e.dwEpCtx0 |= XHCI_EPCTX_0_MAXP_STREAMS_SET(static_cast<uint32_t>(__builtin_ctz(maxStream + 1U) - 1));
	} else {
		if (pRing->cycleState)
			pEpContext->_e.qwEpCtx2 |= 1ULL;
		else
			pEpContext->_e.qwEpCtx2 &= ~1ULL;
	}
	pEpContext->_e.dwEpCtx4 |= XHCI_EPCTX_4_AVG_TRB_LEN_SET(static_cast<uint32_t>(maxPacketSize));
	/*
	 * Note: For SS periodic endpoints, Max ESIT Payload should be taken
	 *   from the SS endpoint companion descriptor, wBytesPerInterval, not
	 *   calculated.  Unfortunately, IOUSBFamily does not pass this parameter.
	 *   The value below is the maximum allowed, which ensures the endpoint
	 *   can operate at its max throughput, but also results in over-provisioning
	 *   of bandwidth, which can cause the configure-endpoint command to
	 *   be rejected with an insufficient-bandwidth error.
	 */
	if ((endpointType | CTRL_EP) == ISOC_IN_EP ||
		(endpointType | CTRL_EP) == INT_IN_EP)
		pEpContext->_e.dwEpCtx4 |= XHCI_EPCTX_4_MAX_ESIT_PAYLOAD_SET(maxPacketSize * (1U + maxBurst) * (1U + multiple));
	SetTRBAddr64(&localTrb, _inputContext.physAddr);
	localTrb.d |= XHCI_TRB_3_SLOT_SET(static_cast<uint32_t>(slot));
	retFromCMD = WaitForCMD(&localTrb, XHCI_TRB_TYPE_CONFIGURE_EP, 0);
	ReleaseInputContext();
	if (retFromCMD == -1)
		return kIOReturnInternalError;
	if (retFromCMD > -1000)
		return kIOReturnSuccess;
	if (retFromCMD == -1000 - XHCI_TRB_ERROR_RESOURCE)
		return kIOUSBEndpointCountExceeded;
	if (retFromCMD == -1000 - XHCI_TRB_ERROR_PARAMETER ||
		retFromCMD == -1000 - XHCI_TRB_ERROR_TRB) {
	}
	return kIOReturnInternalError;
}

__attribute__((visibility("hidden")))
IOReturn CLASS::StartEndpoint(int32_t slot, int32_t endpoint, uint16_t streamId)
{
#if 0
	/*
	 * Added Mavericks
	 */
	ringStruct* pRing = GetRing(slot, endpoint, streamId);
	if (pRing && pRing->needsSetTRDQPtr)
		SetTRDQPtr(slot, endpoint, streamId, pRing->dequeueIndex);
#endif
	Write32Reg(&_pXHCIDoorbellRegisters[slot], (static_cast<uint32_t>(streamId) << 16) | (static_cast<uint32_t>(endpoint) & 0xFFU));
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
void CLASS::StopEndpoint(int32_t slot, int32_t endpoint, bool suspend)
{
	TRBStruct localTrb = { 0 };
	int32_t retFromCMD;

#if 0
	/*
	 * Note: UpdateTimeouts is the only consumer of stopTrb
	 */
	ClearStopTDs(slot, endpoint);
#endif
	localTrb.d |= XHCI_TRB_3_SLOT_SET(slot);
	localTrb.d |= XHCI_TRB_3_EP_SET(endpoint);
	if (suspend)
		localTrb.d |= XHCI_TRB_3_SUSP_EP_BIT;
	retFromCMD = WaitForCMD(&localTrb, XHCI_TRB_TYPE_STOP_EP, 0);
	if (_vendorID == kVendorIntel && retFromCMD == -1000 - 196)	// Intel CC_NOSTOP
		SetNeedsReset(slot, true);
}

__attribute__((visibility("hidden")))
void CLASS::ResetEndpoint(int32_t slot, int32_t endpoint, bool TSP)
{
	TRBStruct localTrb = { 0 };

	localTrb.d |= XHCI_TRB_3_SLOT_SET(slot);
	localTrb.d |= XHCI_TRB_3_EP_SET(endpoint);
	if (TSP)
		localTrb.d |= XHCI_TRB_3_PRSV_BIT;
	WaitForCMD(&localTrb, XHCI_TRB_TYPE_RESET_EP, 0);
}

__attribute__((visibility("hidden")))
uint32_t CLASS::QuiesceEndpoint(int32_t slot, int32_t endpoint)
{
	uint32_t epState;
	ContextStruct volatile* pContext;

#if 0
	/*
	 * Note: Added Mavericks
	 */
	if (!_controllerAvailable)
		return EP_STATE_DISABLED;
#endif
#if 0
	/*
	 * Note: UpdateTimeouts is the only consumer of stopTrb
	 */
	ClearStopTDs(slot, endpoint);
#endif
	pContext = GetSlotContext(slot, endpoint);
	epState = XHCI_EPCTX_0_EPSTATE_GET(pContext->_e.dwEpCtx0);
	switch (epState) {
		case EP_STATE_RUNNING:
			StopEndpoint(slot, endpoint);
			if (XHCI_EPCTX_0_EPSTATE_GET(pContext->_e.dwEpCtx0) != EP_STATE_HALTED)
				break;
			epState = EP_STATE_HALTED;
		case EP_STATE_HALTED:
			ResetEndpoint(slot, endpoint);
			break;
	}
	return epState;
}

__attribute__((visibility("hidden")))
bool CLASS::checkEPForTimeOuts(int32_t slot, int32_t endpoint, uint32_t streamId, uint32_t frameNumber, bool abortAll)
{
	ringStruct* pRing;
	XHCIAsyncEndpoint* pAsyncEp;
	ContextStruct* pEpContext;
	uint32_t ndto;
	uint16_t dq;
	bool stopped = false;

	pRing = GetRing(slot, endpoint, streamId);
	if (!pRing)
		return false;
	dq = pRing->dequeueIndex;
	if (!abortAll && (pRing->lastSeenDequeueIndex != dq || pRing->enqueueIndex == dq)) {
		pRing->lastSeenDequeueIndex = dq;
		return false;
	}
	/*
	 * Note: Isoch Endpoints are ruled out in CheckSlotForTimeouts
	 */
	pAsyncEp = pRing->asyncEndpoint;
	if (!pAsyncEp || !pAsyncEp->scheduledHead)
		return false;
	if (abortAll)
		pEpContext = GetSlotContext(slot, endpoint);
	else if (pAsyncEp->NeedTimeouts()) {
		pEpContext = GetSlotContext(slot, endpoint);
		switch (XHCI_EPCTX_1_EPTYPE_GET(pEpContext->_e.dwEpCtx1)) {
			case BULK_OUT_EP:
			case CTRL_EP:
			case BULK_IN_EP:
				break;
			default:
				return false;
		}
	} else
		return false;
	if (pAsyncEp->scheduledHead->command)
		ndto = pAsyncEp->scheduledHead->command->GetNoDataTimeout();
	else
		ndto = 0U;
	ClearStopTDs(slot, endpoint);
	if (ndto)
		switch (XHCI_EPCTX_0_EPSTATE_GET(pEpContext->_e.dwEpCtx0)) {
			case EP_STATE_DISABLED:
			case EP_STATE_STOPPED:
				break;
			case EP_STATE_RUNNING:
				if (!streamId || abortAll) {
					StopEndpoint(slot, endpoint);
					if (XHCI_EPCTX_0_EPSTATE_GET(pEpContext->_e.dwEpCtx0) == EP_STATE_HALTED)
						ResetEndpoint(slot, endpoint);
					stopped = true;
				}
				break;
			default:
				break;
		}
	pAsyncEp->UpdateTimeouts(abortAll, frameNumber, stopped);
	return stopped;
}

__attribute__((visibility("hidden")))
bool CLASS::IsIsocEP(int32_t slot, int32_t endpoint)
{
	uint32_t epType;
	ContextStruct* pContext = GetSlotContext(slot, endpoint);
	if (!pContext ||
		XHCI_EPCTX_0_EPSTATE_GET(pContext->_e.dwEpCtx0) == EP_STATE_DISABLED)
		return false;
	epType = XHCI_EPCTX_1_EPTYPE_GET(pContext->_e.dwEpCtx1);
	return epType == ISOC_OUT_EP || epType == ISOC_IN_EP;
}

__attribute__((visibility("hidden")))
void CLASS::ClearEndpoint(int32_t slot, int32_t endpoint)
{
	ContextStruct *pContext;
	TRBStruct localTrb = { 0 };
	int32_t retFromCMD;

	GetInputContext();
	pContext = GetInputContextPtr();
	pContext->_ic.dwInCtx0 = XHCI_INCTX_0_DROP_MASK(endpoint);
	pContext->_ic.dwInCtx1 = XHCI_INCTX_1_ADD_MASK(endpoint) | XHCI_INCTX_1_ADD_MASK(0U);
	pContext = GetInputContextPtr(1);
	*pContext = *GetSlotContext(slot);
	PrintContext(pContext);
	pContext->_s.dwSctx0 &= ~(1U << 24);
	pContext->_s.dwSctx3 = 0U;
	bzero(&pContext->_s.pad[0], sizeof pContext->_s.pad[0]);
	pContext = GetInputContextPtr(1 + endpoint);
	*pContext = *GetSlotContext(slot, endpoint);
	bzero(&pContext->_e.pad[0], sizeof pContext->_e.pad[0]);
	PrintContext(pContext);
	SetTRBAddr64(&localTrb, _inputContext.physAddr);
	localTrb.d |= XHCI_TRB_3_SLOT_SET(slot);
	retFromCMD = WaitForCMD(&localTrb, XHCI_TRB_TYPE_CONFIGURE_EP, 0);
	ReleaseInputContext();
	if (retFromCMD != -1 && retFromCMD > -1000)
		return;
	if (retFromCMD == -1000 - XHCI_TRB_ERROR_PARAMETER ||
		retFromCMD == -1000 - XHCI_TRB_ERROR_TRB) {
	}
}

__attribute__((visibility("hidden")))
uint8_t CLASS::TranslateEndpoint(int16_t endpointNumber, int16_t direction)
{
	return static_cast<uint8_t>((2 * endpointNumber) | (direction ? 1 : 0));
}

__attribute__((visibility("hidden")))
void CLASS::DeconfigureEndpoint(uint8_t slot, uint8_t endpoint, bool parkRing)
{
	TRBStruct localTrb = { 0 };
	ContextStruct* pContext;
	int32_t prevNumCtx, numCtx;
	if (QuiesceEndpoint(slot, endpoint) == EP_STATE_DISABLED)
		return;
	if (parkRing)
		ParkRing(slot, endpoint);
	if (endpoint < 2U)
		return;
	pContext = GetSlotContext(slot);
	prevNumCtx = numCtx = static_cast<int32_t>(XHCI_SCTX_0_CTX_NUM_GET(pContext->_s.dwSctx0));
	if (numCtx == endpoint) {
		SlotStruct const* pSlot = ConstSlotPtr(slot);
		for (--numCtx; numCtx > 1 && pSlot->ringArrayForEndpoint[numCtx]->isInactive(); --numCtx);
	}
	if (numCtx == 1) {
		localTrb.d |= XHCI_TRB_3_SLOT_SET(static_cast<uint32_t>(slot)) | XHCI_TRB_3_DCEP_BIT;
		WaitForCMD(&localTrb, XHCI_TRB_TYPE_CONFIGURE_EP, 0);
		return;
	}
	GetInputContext();
	pContext = GetInputContextPtr();
	pContext->_ic.dwInCtx0 = XHCI_INCTX_0_DROP_MASK(endpoint);
	if (numCtx != prevNumCtx) {
		pContext->_ic.dwInCtx1 = XHCI_INCTX_1_ADD_MASK(0U);
		pContext = GetInputContextPtr(1);
		*pContext = *GetSlotContext(slot);
		pContext->_s.dwSctx0 &= ~XHCI_SCTX_0_CTX_NUM_SET(0x1FU);
		pContext->_s.dwSctx0 |= XHCI_SCTX_0_CTX_NUM_SET(numCtx);
		pContext->_s.dwSctx0 &= ~(1U << 24);
	}
	SetTRBAddr64(&localTrb, _inputContext.physAddr);
	localTrb.d |= XHCI_TRB_3_SLOT_SET(static_cast<uint32_t>(slot));
	WaitForCMD(&localTrb, XHCI_TRB_TYPE_CONFIGURE_EP, 0);
	ReleaseInputContext();
}
