/*
 * manager.cxx
 *
 * Media channels abstraction
 *
 * Open Phone Abstraction Library (OPAL)
 * Formally known as the Open H323 project.
 *
 * Copyright (c) 2001 Equivalence Pty. Ltd.
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.0 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is Open Phone Abstraction Library.
 *
 * The Initial Developer of the Original Code is Equivalence Pty. Ltd.
 *
 * Contributor(s): ______________________________________.
 *
 * $Revision$
 * $Author$
 * $Date$
 */

#include <ptlib.h>

#ifdef __GNUC__
#pragma implementation "manager.h"
#endif

#include <opal/manager.h>

#include <opal/endpoint.h>
#include <opal/call.h>
#include <opal/patch.h>
#include <opal/mediastrm.h>

#if OPAL_VIDEO
#include <codec/vidcodec.h>
#endif

#include <h224/h224handler.h>
#include <h224/h281handler.h>

#include "../../version.h"


#ifndef IPTOS_PREC_CRITIC_ECP
#define IPTOS_PREC_CRITIC_ECP (5 << 5)
#endif

#ifndef IPTOS_LOWDELAY
#define IPTOS_LOWDELAY 0x10
#endif


static const char * const DefaultMediaFormatOrder[] = {
  OPAL_G7231_6k3,
  OPAL_G729B,
  OPAL_G729AB,
  OPAL_G729,
  OPAL_G729A,
  OPAL_GSM0610,
  OPAL_G728,
  OPAL_G711_ULAW_64K,
  OPAL_G711_ALAW_64K
};

#define new PNEW


/////////////////////////////////////////////////////////////////////////////

PString OpalGetVersion()
{
#define AlphaCode   "alpha"
#define BetaCode    "beta"
#define ReleaseCode "."

  return psprintf("%u.%u%s%u", MAJOR_VERSION, MINOR_VERSION, BUILD_TYPE, BUILD_NUMBER);
}


unsigned OpalGetMajorVersion()
{
  return MAJOR_VERSION;
}

unsigned OpalGetMinorVersion()
{
  return MINOR_VERSION;
}

unsigned OpalGetBuildNumber()
{
  return BUILD_NUMBER;
}


OpalProductInfo::OpalProductInfo()
  : vendor(PProcess::Current().GetManufacturer())
  , name(PProcess::Current().GetName())
  , version(PProcess::Current().GetVersion())
  , t35CountryCode(9)     // Country code for Australia
  , t35Extension(0)       // No extension code for Australia
  , manufacturerCode(61)  // Allocated by Australian Communications Authority, Oct 2000;
{
}

OpalProductInfo & OpalProductInfo::Default()
{
  static OpalProductInfo instance;
  return instance;
}


PCaselessString OpalProductInfo::AsString() const
{
  PStringStream str;
  str << name << '\t' << version << '\t';
  if (t35CountryCode != 0 && manufacturerCode != 0) {
    str << (unsigned)t35CountryCode;
    if (t35Extension != 0)
      str << '.' << (unsigned)t35Extension;
    str << '/' << manufacturerCode;
  }
  str << '\t' << vendor;
  return str;
}


/////////////////////////////////////////////////////////////////////////////

#ifdef _MSC_VER
#pragma warning(disable:4355)
#endif

OpalManager::OpalManager()
  : defaultUserName(PProcess::Current().GetUserName())
  , defaultDisplayName(defaultUserName)
#ifdef _WIN32
  , rtpIpTypeofService(IPTOS_PREC_CRITIC_ECP|IPTOS_LOWDELAY)
#else
  , rtpIpTypeofService(IPTOS_LOWDELAY) // Don't use IPTOS_PREC_CRITIC_ECP on Unix platforms as then need to be root
#endif
  , minAudioJitterDelay(50)  // milliseconds
  , maxAudioJitterDelay(250) // milliseconds
  , mediaFormatOrder(PARRAYSIZE(DefaultMediaFormatOrder), DefaultMediaFormatOrder)
  , noMediaTimeout(0, 0, 5)     // Minutes
  , translationAddress(0)       // Invalid address to disable
  , stun(NULL)
  , interfaceMonitor(NULL)
  , activeCalls(*this)
  , clearingAllCalls(FALSE)
#if OPAL_RTP_AGGREGATE
  , useRTPAggregation(TRUE)
#endif
{
  rtpIpPorts.current = rtpIpPorts.base = 5000;
  rtpIpPorts.max = 5999;

  // use dynamic port allocation by default
  tcpPorts.current = tcpPorts.base = tcpPorts.max = 0;
  udpPorts.current = udpPorts.base = udpPorts.max = 0;

#ifndef NO_OPAL_VIDEO
  PStringList devices;
  
  devices = PVideoInputDevice::GetDriversDeviceNames("*"); // Get all devices on all drivers
  PINDEX i;
  for (i = 0; i < devices.GetSize(); ++i) {
    if ((devices[i] *= "*.yuv") || (devices[i] *= "fake")) 
      continue;
    videoInputDevice.deviceName = devices[i];
    break;
  }
  autoStartTransmitVideo = !videoInputDevice.deviceName.IsEmpty();

  devices = PVideoOutputDevice::GetDriversDeviceNames("*"); // Get all devices on all drivers
  for (i = 0; i < devices.GetSize(); ++i) {
    if ((devices[i] *= "*.yuv") || (devices[i] *= "null"))
      continue;
    videoOutputDevice.deviceName = devices[i];
    break;
  }
  autoStartReceiveVideo = !videoOutputDevice.deviceName.IsEmpty();

  if (autoStartReceiveVideo)
    videoPreviewDevice = videoOutputDevice;
#endif

  garbageCollector = PThread::Create(PCREATE_NOTIFIER(GarbageMain), 0,
                                     PThread::NoAutoDeleteThread,
                                     PThread::LowPriority,
                                     "OpalGarbage");

  PTRACE(4, "OpalMan\tCreated manager.");
}

#ifdef _MSC_VER
#pragma warning(default:4355)
#endif


OpalManager::~OpalManager()
{
  // Clear any pending calls on this endpoint
  ClearAllCalls();

  // Kill off the endpoints, could wait till compiler generated destructor but
  // prefer to keep the PTRACE's in sequence.
  endpoints.RemoveAll();

  // Shut down the cleaner thread
  garbageCollectExit.Signal();
  garbageCollector->WaitForTermination();

  // Clean up any calls that the cleaner thread missed
  GarbageCollection();

  delete garbageCollector;

  delete stun;
  delete interfaceMonitor;

  PTRACE(4, "OpalMan\tDeleted manager.");
}


void OpalManager::AttachEndPoint(OpalEndPoint * endpoint)
{
  if (PAssertNULL(endpoint) == NULL)
    return;

  endpointsMutex.StartWrite();

  if (endpoints.GetObjectsIndex(endpoint) == P_MAX_INDEX)
    endpoints.Append(endpoint);

  endpointsMutex.EndWrite();
}


void OpalManager::DetachEndPoint(OpalEndPoint * endpoint)
{
  if (PAssertNULL(endpoint) == NULL)
    return;

  endpointsMutex.StartWrite();
  endpoints.Remove(endpoint);
  endpointsMutex.EndWrite();
}


OpalEndPoint * OpalManager::FindEndPoint(const PString & prefix)
{
  PReadWaitAndSignal mutex(endpointsMutex);

  for (PINDEX i = 0; i < endpoints.GetSize(); i++) {
    if (endpoints[i].GetPrefixName() *= prefix)
      return &endpoints[i];
  }

  return NULL;
}


BOOL OpalManager::SetUpCall(const PString & partyA,
                            const PString & partyB,
                            PString & token,
                            void * userData,
                            unsigned int options,
                            OpalConnection::StringOptions * stringOptions)
{
  PTRACE(3, "OpalMan\tSet up call from " << partyA << " to " << partyB);

  OpalCall * call = CreateCall(userData);
  token = call->GetToken();

  call->SetPartyB(partyB);

  // If we are the A-party then need to initiate a call now in this thread and
  // go through the routing engine via OnIncomingConnection. If we were the
  // B-Party then SetUpConnection() gets called in the context of the A-party
  // thread.
  if (MakeConnection(*call, partyA, userData, options, stringOptions) && call->GetConnection(0)->SetUpConnection()) {
    PTRACE(3, "OpalMan\tSetUpCall succeeded, call=" << *call);
    return TRUE;
  }

  PSafePtr<OpalConnection> connection = call->GetConnection(0);
  OpalConnection::CallEndReason endReason = connection != NULL ? connection->GetCallEndReason() : OpalConnection::NumCallEndReasons;
  call->Clear(endReason != OpalConnection::NumCallEndReasons ? endReason : OpalConnection::EndedByTemporaryFailure);

  if (!activeCalls.RemoveAt(token)) {
    PTRACE(2, "OpalMan\tSetUpCall could not remove call from active call list");
  }

  token.MakeEmpty();

  return FALSE;
}


void OpalManager::OnEstablishedCall(OpalCall & /*call*/)
{
}


BOOL OpalManager::IsCallEstablished(const PString & token)
{
  PSafePtr<OpalCall> call = activeCalls.FindWithLock(token, PSafeReadOnly);
  if (call == NULL)
    return FALSE;

  return call->IsEstablished();
}


BOOL OpalManager::ClearCall(const PString & token,
                            OpalConnection::CallEndReason reason,
                            PSyncPoint * sync)
{
  /*The hugely multi-threaded nature of the OpalCall objects means that
    to avoid many forms of race condition, a call is cleared by moving it from
    the "active" call dictionary to a list of calls to be cleared that will be
    processed by a background thread specifically for the purpose of cleaning
    up cleared calls. So that is all that this function actually does.
    The real work is done in the OpalGarbageCollector thread.
   */

  {
    // Find the call by token, callid or conferenceid
    PSafePtr<OpalCall> call = activeCalls.FindWithLock(token, PSafeReference);
    if (call == NULL)
      return FALSE;

    call->Clear(reason, sync);
  }

  if (sync != NULL)
    sync->Wait();

  return TRUE;
}


BOOL OpalManager::ClearCallSynchronous(const PString & token,
                                       OpalConnection::CallEndReason reason)
{
  PSyncPoint wait;
  return ClearCall(token, reason, &wait);
}


void OpalManager::ClearAllCalls(OpalConnection::CallEndReason reason, BOOL wait)
{
  // Remove all calls from the active list first
  for (PSafePtr<OpalCall> call = activeCalls; call != NULL; ++call) {
    call->Clear(reason);
  }

  if (wait) {
    clearingAllCalls = TRUE;
    allCallsCleared.Wait();
    clearingAllCalls = FALSE;
  }
}


void OpalManager::OnClearedCall(OpalCall & PTRACE_PARAM(call))
{
  PTRACE(3, "OpalMan\tOnClearedCall " << call << " from \"" << call.GetPartyA() << "\" to \"" << call.GetPartyB() << '"');
}


OpalCall * OpalManager::CreateCall()
{
  return new OpalCall(*this);
}

OpalCall * OpalManager::CreateCall(void * /*userData*/)
{
  return CreateCall();
}


void OpalManager::DestroyCall(OpalCall * call)
{
  delete call;
}


PString OpalManager::GetNextCallToken()
{
  return psprintf("%u", ++lastCallTokenID);
}

BOOL OpalManager::MakeConnection(OpalCall & call, const PString & remoteParty, void * userData, unsigned int options, OpalConnection::StringOptions * stringOptions)
{
  PTRACE(3, "OpalMan\tSet up connection to \"" << remoteParty << '"');

  if (remoteParty.IsEmpty() || endpoints.IsEmpty())
    return FALSE;

  PCaselessString epname = remoteParty.Left(remoteParty.Find(':'));

  PReadWaitAndSignal mutex(endpointsMutex);

  if (epname.IsEmpty())
    epname = endpoints[0].GetPrefixName();

  for (PINDEX i = 0; i < endpoints.GetSize(); i++) {
    if (epname == endpoints[i].GetPrefixName()) {
      if (endpoints[i].MakeConnection(call, remoteParty, userData, options, stringOptions))
        return TRUE;
    }
  }

  PTRACE(1, "OpalMan\tCould not find endpoint to handle protocol \"" << epname << '"');
  return FALSE;
}

BOOL OpalManager::OnIncomingConnection(OpalConnection & /*connection*/)
{
  return TRUE;
}

BOOL OpalManager::OnIncomingConnection(OpalConnection & /*connection*/, unsigned /*options*/)
{
  return TRUE;
}

BOOL OpalManager::OnIncomingConnection(OpalConnection & connection, unsigned options, OpalConnection::StringOptions * stringOptions)
{
  PTRACE(3, "OpalMan\tOn incoming connection " << connection);

  if (!OnIncomingConnection(connection))
    return FALSE;

  if (!OnIncomingConnection(connection, options))
    return FALSE;

  OpalCall & call = connection.GetCall();

  // See if we already have a B-Party in the call. If not, make one.
  if (call.GetOtherPartyConnection(connection) != NULL)
    return TRUE;

  // Use a routing algorithm to figure out who the B-Party is, then make a connection
  return MakeConnection(call, OnRouteConnection(connection), NULL, options, stringOptions);
}


PString OpalManager::OnRouteConnection(OpalConnection & connection)
{
  // See if have pre-allocated B party address, otherwise use routing algorithm
  PString addr = connection.GetCall().GetPartyB();
  if (addr.IsEmpty()) {
    addr = connection.GetDestinationAddress();

    // No address, fail call
    if (addr.IsEmpty())
      return addr;
  }

  // Have explicit protocol defined, so no translation to be done
  PINDEX colon = addr.Find(':');
  if (colon != P_MAX_INDEX && FindEndPoint(addr.Left(colon)) != NULL)
    return addr;

  // No routes specified, just return what we've got so far, maybe it will work
  if (routeTable.IsEmpty())
    return addr;

  return ApplyRouteTable(connection.GetEndPoint().GetPrefixName(), addr);
}


void OpalManager::OnAlerting(OpalConnection & connection)
{
  PTRACE(3, "OpalMan\tOnAlerting " << connection);

  connection.GetCall().OnAlerting(connection);
}

OpalConnection::AnswerCallResponse
       OpalManager::OnAnswerCall(OpalConnection & connection,
                                  const PString & caller)
{
  PTRACE(3, "OpalMan\tOnAnswerCall " << connection);

  return connection.GetCall().OnAnswerCall(connection, caller);
}

void OpalManager::OnConnected(OpalConnection & connection)
{
  PTRACE(3, "OpalMan\tOnConnected " << connection);

  connection.GetCall().OnConnected(connection);
}


void OpalManager::OnEstablished(OpalConnection & connection)
{
  PTRACE(3, "OpalMan\tOnEstablished " << connection);

  connection.GetCall().OnEstablished(connection);
}


void OpalManager::OnReleased(OpalConnection & connection)
{
  PTRACE(3, "OpalMan\tOnReleased " << connection);

  connection.GetCall().OnReleased(connection);
}


void OpalManager::OnHold(OpalConnection & PTRACE_PARAM(connection))
{
  PTRACE(3, "OpalMan\tOnHold " << connection);
}


BOOL OpalManager::OnForwarded(OpalConnection & PTRACE_PARAM(connection),
			      const PString & /*forwardParty*/)
{
  PTRACE(4, "OpalEP\tOnForwarded " << connection);
  return TRUE;
}


void OpalManager::AdjustMediaFormats(const OpalConnection & /*connection*/,
                                     OpalMediaFormatList & mediaFormats) const
{
  mediaFormats.Remove(mediaFormatMask);
  mediaFormats.Reorder(mediaFormatOrder);
  for (PINDEX i = 0; i < mediaFormats.GetSize(); i++)
    mediaFormats[i].ToCustomisedOptions();
}


BOOL OpalManager::IsMediaBypassPossible(const OpalConnection & source,
                                        const OpalConnection & destination,
                                        unsigned sessionID) const
{
  PTRACE(3, "OpalMan\tIsMediaBypassPossible: session " << sessionID);

  return source.IsMediaBypassPossible(sessionID) &&
         destination.IsMediaBypassPossible(sessionID);
}


BOOL OpalManager::OnOpenMediaStream(OpalConnection & connection,
                                    OpalMediaStream & stream)
{
  PTRACE(3, "OpalMan\tOnOpenMediaStream " << connection << ',' << stream);

  if (stream.IsSource())
    return connection.GetCall().PatchMediaStreams(connection, stream);

  return TRUE;
}


void OpalManager::OnRTPStatistics(const OpalConnection & connection, const RTP_Session & session)
{
  connection.GetCall().OnRTPStatistics(connection, session);
}


void OpalManager::OnClosedMediaStream(const OpalMediaStream & /*channel*/)
{
}

#if OPAL_VIDEO

void OpalManager::AddVideoMediaFormats(OpalMediaFormatList & mediaFormats,
                                       const OpalConnection * /*connection*/) const
{
  if (videoInputDevice.deviceName.IsEmpty())
      return;

  mediaFormats += OpalYUV420P;
  mediaFormats += OpalRGB32;
  mediaFormats += OpalRGB24;
}


BOOL OpalManager::CreateVideoInputDevice(const OpalConnection & /*connection*/,
                                         const OpalMediaFormat & mediaFormat,
                                         PVideoInputDevice * & device,
                                         BOOL & autoDelete)
{
  // Make copy so we can adjust the size
  PVideoDevice::OpenArgs args = videoInputDevice;
  args.width = mediaFormat.GetOptionInteger(OpalVideoFormat::FrameWidthOption(), PVideoFrameInfo::QCIFWidth);
  args.height = mediaFormat.GetOptionInteger(OpalVideoFormat::FrameHeightOption(), PVideoFrameInfo::QCIFHeight);

  autoDelete = TRUE;
  device = PVideoInputDevice::CreateOpenedDevice(args);
  PTRACE_IF(2, device == NULL, "OpalCon\tCould not open video device \"" << args.deviceName << '"');
  return device != NULL;
}


BOOL OpalManager::CreateVideoOutputDevice(const OpalConnection & connection,
                                          const OpalMediaFormat & mediaFormat,
                                          BOOL preview,
                                          PVideoOutputDevice * & device,
                                          BOOL & autoDelete)
{
  // Donot use our one and only SDl window, if we need it for the video output.
  if (preview && videoPreviewDevice.driverName == "SDL" && videoOutputDevice.driverName == "SDL")
    return FALSE;

  // Make copy so we can adjust the size
  PVideoDevice::OpenArgs args = preview ? videoPreviewDevice : videoOutputDevice;
  args.width = mediaFormat.GetOptionInteger(OpalVideoFormat::FrameWidthOption(), PVideoFrameInfo::QCIFWidth);
  args.height = mediaFormat.GetOptionInteger(OpalVideoFormat::FrameHeightOption(), PVideoFrameInfo::QCIFHeight);

  PINDEX start = args.deviceName.Find("TITLE=\"");
  if (start != P_MAX_INDEX) {
    start += 7;
    args.deviceName.Splice(preview ? "Local Preview" : connection.GetRemotePartyName(), start, args.deviceName.Find('"', start)-start);
  }

  autoDelete = TRUE;
  device = PVideoOutputDevice::CreateOpenedDevice(args);
  return device != NULL;
}

#endif // OPAL_VIDEO


OpalMediaPatch * OpalManager::CreateMediaPatch(OpalMediaStream & source,
                                               BOOL requiresPatchThread)
{
  if (requiresPatchThread) {
    return new OpalMediaPatch(source);
  } else {
    return new OpalPassiveMediaPatch(source);
  }
}


void OpalManager::DestroyMediaPatch(OpalMediaPatch * patch)
{
  delete patch;
}


BOOL OpalManager::OnStartMediaPatch(const OpalMediaPatch & /*patch*/)
{
  return TRUE;
}


void OpalManager::OnUserInputString(OpalConnection & connection,
                                    const PString & value)
{
  connection.GetCall().OnUserInputString(connection, value);
}


void OpalManager::OnUserInputTone(OpalConnection & connection,
                                  char tone,
                                  int duration)
{
  connection.GetCall().OnUserInputTone(connection, tone, duration);
}


PString OpalManager::ReadUserInput(OpalConnection & connection,
                                  const char * terminators,
                                  unsigned lastDigitTimeout,
                                  unsigned firstDigitTimeout)
{
  PTRACE(3, "OpalCon\tReadUserInput from " << connection);

  connection.PromptUserInput(TRUE);
  PString digit = connection.GetUserInput(firstDigitTimeout);
  connection.PromptUserInput(FALSE);

  if (digit.IsEmpty()) {
    PTRACE(2, "OpalCon\tReadUserInput first character timeout (" << firstDigitTimeout << "ms) on " << *this);
    return PString::Empty();
  }

  PString input;
  while (digit.FindOneOf(terminators) == P_MAX_INDEX) {
    input += digit;

    digit = connection.GetUserInput(lastDigitTimeout);
    if (digit.IsEmpty()) {
      PTRACE(2, "OpalCon\tReadUserInput last character timeout (" << lastDigitTimeout << "ms) on " << *this);
      return input; // Input so far will have to do
    }
  }

  return input.IsEmpty() ? digit : input;
}

#if OPAL_T120DATA

OpalT120Protocol * OpalManager::CreateT120ProtocolHandler(const OpalConnection & ) const
{
  return NULL;
}

#endif

#if OPAL_T38FAX

OpalT38Protocol * OpalManager::CreateT38ProtocolHandler(const OpalConnection & ) const
{
  return NULL;
}

#endif

#ifdef OPAL_H224

OpalH224Handler * OpalManager::CreateH224ProtocolHandler(OpalConnection & connection,
														 unsigned sessionID) const
{
  return new OpalH224Handler(connection, sessionID);
}

OpalH281Handler * OpalManager::CreateH281ProtocolHandler(OpalH224Handler & h224Handler) const
{
  return new OpalH281Handler(h224Handler);
}

#endif

OpalManager::RouteEntry::RouteEntry(const PString & pat, const PString & dest)
  : pattern(pat),
    destination(dest),
    regex('^'+pat+'$')
{
}


void OpalManager::RouteEntry::PrintOn(ostream & strm) const
{
  strm << pattern << '=' << destination;
}


BOOL OpalManager::AddRouteEntry(const PString & spec)
{
  if (spec[0] == '#') // Comment
    return FALSE;

  if (spec[0] == '@') { // Load from file
    PTextFile file;
    if (!file.Open(spec.Mid(1), PFile::ReadOnly)) {
      PTRACE(1, "OpalMan\tCould not open route file \"" << file.GetFilePath() << '"');
      return FALSE;
    }
    PTRACE(4, "OpalMan\tAdding routes from file \"" << file.GetFilePath() << '"');
    BOOL ok = FALSE;
    PString line;
    while (file.good()) {
      file >> line;
      if (AddRouteEntry(line))
        ok = TRUE;
    }
    return ok;
  }

  PINDEX equal = spec.Find('=');
  if (equal == P_MAX_INDEX) {
    PTRACE(2, "OpalMan\tInvalid route table entry: \"" << spec << '"');
    return FALSE;
  }

  RouteEntry * entry = new RouteEntry(spec.Left(equal).Trim(), spec.Mid(equal+1).Trim());
  if (entry->regex.GetErrorCode() != PRegularExpression::NoError) {
    PTRACE(2, "OpalMan\tIllegal regular expression in route table entry: \"" << spec << '"');
    delete entry;
    return FALSE;
  }

  PTRACE(4, "OpalMan\tAdded route \"" << *entry << '"');
  routeTableMutex.Wait();
  routeTable.Append(entry);
  routeTableMutex.Signal();
  return TRUE;
}


BOOL OpalManager::SetRouteTable(const PStringArray & specs)
{
  BOOL ok = FALSE;

  routeTableMutex.Wait();
  routeTable.RemoveAll();

  for (PINDEX i = 0; i < specs.GetSize(); i++) {
    if (AddRouteEntry(specs[i].Trim()))
      ok = TRUE;
  }

  routeTableMutex.Signal();

  return ok;
}


void OpalManager::SetRouteTable(const RouteTable & table)
{
  routeTableMutex.Wait();
  routeTable = table;
  routeTable.MakeUnique();
  routeTableMutex.Signal();
}


PString OpalManager::ApplyRouteTable(const PString & proto, const PString & addr)
{
  PWaitAndSignal mutex(routeTableMutex);

  PString destination;
  PString search = proto + ':' + addr;
  PTRACE(4, "OpalMan\tSearching for route \"" << search << '"');
  for (PINDEX i = 0; i < routeTable.GetSize(); i++) {
    RouteEntry & entry = routeTable[i];
    PINDEX pos;
    if (entry.regex.Execute(search, pos)) {
      destination = routeTable[i].destination;
      break;
    }
  }

  if (destination.IsEmpty())
    return PString::Empty();

  destination.Replace("<da>", addr);

  PINDEX pos;
  if ((pos = destination.Find("<dn>")) != P_MAX_INDEX)
    destination.Splice(addr.Left(addr.FindSpan("0123456789*#")), pos, 4);

  if ((pos = destination.Find("<!dn>")) != P_MAX_INDEX)
    destination.Splice(addr.Mid(addr.FindSpan("0123456789*#")), pos, 5);

  // Do meta character substitutions
  if ((pos = destination.Find("<dn2ip>")) != P_MAX_INDEX) {
    PStringStream route;
    PStringArray stars = addr.Tokenise('*');
    switch (stars.GetSize()) {
      case 0 :
      case 1 :
      case 2 :
      case 3 :
        route << addr;
        break;

      case 4 :
        route << stars[0] << '.' << stars[1] << '.'<< stars[2] << '.'<< stars[3];
        break;

      case 5 :
        route << stars[0] << '@'
              << stars[1] << '.' << stars[2] << '.'<< stars[3] << '.'<< stars[4];
        break;

      default :
        route << stars[0] << '@'
              << stars[1] << '.' << stars[2] << '.'<< stars[3] << '.'<< stars[4]
              << ':' << stars[5];
        break;
    }
    destination.Splice(route, pos, 7);
  }

  return destination;
}


BOOL OpalManager::IsLocalAddress(const PIPSocket::Address & ip) const
{
  /* Check if the remote address is a private IP, broadcast, or us */
  return ip.IsAny() || ip.IsBroadcast() || ip.IsRFC1918() || PIPSocket::IsLocalHost(ip);
}


BOOL OpalManager::TranslateIPAddress(PIPSocket::Address & localAddress,
                                     const PIPSocket::Address & remoteAddress)
{
  if (!translationAddress.IsValid())
    return FALSE; // Have nothing to translate it to

  if (!IsLocalAddress(localAddress))
    return FALSE; // Is already translated

  if (IsLocalAddress(remoteAddress))
    return FALSE; // Does not need to be translated

  // Tranlsate it!
  localAddress = translationAddress;
  return TRUE;
}


PSTUNClient * OpalManager::GetSTUN(const PIPSocket::Address & ip) const
{
  if (ip.IsValid() && IsLocalAddress(ip))
    return NULL;

  return stun;
}


PSTUNClient::NatTypes OpalManager::SetSTUNServer(const PString & server)
{
  stunServer = server;

  if (server.IsEmpty()) {
    if (stun) {
      PInterfaceMonitor::GetInstance().OnRemoveSTUNClient(stun);
    }
    delete stun;
    delete interfaceMonitor;
    stun = NULL;
    interfaceMonitor = NULL;
    return PSTUNClient::UnknownNat;
  }

  if (stun == NULL) {
    stun = new PSTUNClient(server,
                           GetUDPPortBase(), GetUDPPortMax(),
                           GetRtpIpPortBase(), GetRtpIpPortMax());
    interfaceMonitor = new InterfaceMonitor(stun);
  } else
    stun->SetServer(server);

  PSTUNClient::NatTypes type = stun->GetNatType();
  if (type != PSTUNClient::BlockedNat)
    stun->GetExternalAddress(translationAddress);

  PTRACE(3, "OPAL\tSTUN server \"" << server << "\" replies " << type << ", external IP " << translationAddress);

  return type;
}


void OpalManager::PortInfo::Set(unsigned newBase,
                                unsigned newMax,
                                unsigned range,
                                unsigned dflt)
{
  if (newBase == 0) {
    newBase = dflt;
    newMax = dflt;
    if (dflt > 0)
      newMax += range;
  }
  else {
    if (newBase < 1024)
      newBase = 1024;
    else if (newBase > 65500)
      newBase = 65500;

    if (newMax <= newBase)
      newMax = newBase + range;
    if (newMax > 65535)
      newMax = 65535;
  }

  mutex.Wait();

  current = base = (WORD)newBase;
  max = (WORD)newMax;

  mutex.Signal();
}


WORD OpalManager::PortInfo::GetNext(unsigned increment)
{
  PWaitAndSignal m(mutex);

  if (current < base || current >= (max-increment))
    current = base;

  if (current == 0)
    return 0;

  WORD p = current;
  current = (WORD)(current + increment);
  return p;
}


void OpalManager::SetTCPPorts(unsigned tcpBase, unsigned tcpMax)
{
  tcpPorts.Set(tcpBase, tcpMax, 49, 0);
}


WORD OpalManager::GetNextTCPPort()
{
  return tcpPorts.GetNext(1);
}


void OpalManager::SetUDPPorts(unsigned udpBase, unsigned udpMax)
{
  udpPorts.Set(udpBase, udpMax, 99, 0);

  if (stun != NULL)
    stun->SetPortRanges(GetUDPPortBase(), GetUDPPortMax(), GetRtpIpPortBase(), GetRtpIpPortMax());
}


WORD OpalManager::GetNextUDPPort()
{
  return udpPorts.GetNext(1);
}


void OpalManager::SetRtpIpPorts(unsigned rtpIpBase, unsigned rtpIpMax)
{
  rtpIpPorts.Set((rtpIpBase+1)&0xfffe, rtpIpMax&0xfffe, 199, 5000);

  if (stun != NULL)
    stun->SetPortRanges(GetUDPPortBase(), GetUDPPortMax(), GetRtpIpPortBase(), GetRtpIpPortMax());
}


WORD OpalManager::GetRtpIpPortPair()
{
  return rtpIpPorts.GetNext(2);
}


void OpalManager::SetAudioJitterDelay(unsigned minDelay, unsigned maxDelay)
{
  PAssert(minDelay <= 10000 && maxDelay <= 10000, PInvalidParameter);

  if (minDelay < 10)
    minDelay = 10;
  minAudioJitterDelay = minDelay;

  if (maxDelay < minDelay)
    maxDelay = minDelay;
  maxAudioJitterDelay = maxDelay;
}


#if OPAL_VIDEO
template<class PVideoXxxDevice>
static BOOL SetVideoDevice(const PVideoDevice::OpenArgs & args, PVideoDevice::OpenArgs & member)
{
  // Check that the input device is legal
  PVideoXxxDevice * pDevice = PVideoXxxDevice::CreateDeviceByName(args.deviceName, args.driverName, args.pluginMgr);
  if (pDevice != NULL) {
    delete pDevice;
    member = args;
    return TRUE;
  }

  if (args.deviceName[0] != '#')
    return FALSE;

  // Selected device by ordinal
  PStringList devices = PVideoXxxDevice::GetDriversDeviceNames(args.driverName, args.pluginMgr);
  if (devices.IsEmpty())
    return FALSE;

  PINDEX id = args.deviceName.Mid(1).AsUnsigned();
  if (id <= 0 || id > devices.GetSize())
    return FALSE;

  member = args;
  member.deviceName = devices[id-1];
  return TRUE;
}


BOOL OpalManager::SetVideoInputDevice(const PVideoDevice::OpenArgs & args)
{
  return SetVideoDevice<PVideoInputDevice>(args, videoInputDevice);
}


BOOL OpalManager::SetVideoPreviewDevice(const PVideoDevice::OpenArgs & args)
{
  return SetVideoDevice<PVideoOutputDevice>(args, videoPreviewDevice);
}


BOOL OpalManager::SetVideoOutputDevice(const PVideoDevice::OpenArgs & args)
{
  return SetVideoDevice<PVideoOutputDevice>(args, videoOutputDevice);
}

#endif

BOOL OpalManager::SetNoMediaTimeout(const PTimeInterval & newInterval) 
{
  if (newInterval < 10)
    return FALSE;

  noMediaTimeout = newInterval; 
  return TRUE; 
}


void OpalManager::GarbageCollection()
{
  BOOL allCleared = activeCalls.DeleteObjectsToBeRemoved();

  endpointsMutex.StartRead();

  for (PINDEX i = 0; i < endpoints.GetSize(); i++) {
    if (!endpoints[i].GarbageCollection())
      allCleared = FALSE;
  }

  endpointsMutex.EndRead();

  if (allCleared && clearingAllCalls)
    allCallsCleared.Signal();
}


void OpalManager::CallDict::DeleteObject(PObject * object) const
{
  manager.DestroyCall(PDownCast(OpalCall, object));
}


void OpalManager::GarbageMain(PThread &, INT)
{
  while (!garbageCollectExit.Wait(1000))
    GarbageCollection();
}

void OpalManager::OnNewConnection(OpalConnection & /*conn*/)
{
}

BOOL OpalManager::UseRTPAggregation() const
{ 
#if OPAL_RTP_AGGREGATE
  return useRTPAggregation; 
#else
  return FALSE;
#endif
}

BOOL OpalManager::StartRecording(const PString & callToken, const PFilePath & fn)
{
  PSafePtr<OpalCall> call = activeCalls.FindWithLock(callToken, PSafeReadWrite);
  if (call == NULL)
    return FALSE;

  return call->StartRecording(fn);
}

void OpalManager::StopRecording(const PString & callToken)
{
  PSafePtr<OpalCall> call = activeCalls.FindWithLock(callToken, PSafeReadWrite);
  if (call != NULL)
    call->StopRecording();
}


BOOL OpalManager::IsRTPNATEnabled(OpalConnection & /*conn*/, 
                    const PIPSocket::Address & localAddr, 
                    const PIPSocket::Address & peerAddr,
                    const PIPSocket::Address & sigAddr,
                                          BOOL incoming)
{
  BOOL remoteIsNAT = FALSE;

  PTRACE(4, "OPAL\tChecking " << (incoming ? "incoming" : "outgoing") << " call for NAT: local=" << localAddr << ",peer=" << peerAddr << ",sig=" << sigAddr);

  if (incoming) {

    // by default, only check for NAT under two conditions
    //    1. Peer is not local, but the peer thinks it is
    //    2. Peer address and local address are both private, but not the same
    //
    if ((!peerAddr.IsRFC1918() && sigAddr.IsRFC1918()) ||
        ((peerAddr.IsRFC1918() && localAddr.IsRFC1918()) && (localAddr != peerAddr))) {

      // given these paramaters, translate the local address
      PIPSocket::Address trialAddr = localAddr;
      TranslateIPAddress(trialAddr, peerAddr);

      PTRACE(3, "OPAL\tTranslateIPAddress has converted " << localAddr << " to " << trialAddr);

      // if the application specific routine changed the local address, then enable RTP NAT mode
      if (localAddr != trialAddr) {
        PTRACE(3, "OPAL\tSource signal address " << sigAddr << " and peer address " << peerAddr << " indicate remote endpoint is behind NAT");
        remoteIsNAT = TRUE;
      }
    }
  }
  else
  {
  }

  return remoteIsNAT;
}


/////////////////////////////////////////////////////////////////////////////

OpalManager::InterfaceMonitor::InterfaceMonitor(PSTUNClient * _stun)
: PInterfaceMonitorClient(OpalManagerInterfaceMonitorClientPriority),
  stun(_stun)
{
}

void OpalManager::InterfaceMonitor::OnAddInterface(const PIPSocket::InterfaceEntry & /*entry*/)
{
  stun->InvalidateExternalAddressCache();
}

void OpalManager::InterfaceMonitor::OnRemoveInterface(const PIPSocket::InterfaceEntry & /*entry*/)
{
  stun->InvalidateExternalAddressCache();
}


/////////////////////////////////////////////////////////////////////////////

OpalRecordManager::Mixer_T::Mixer_T()
  : OpalAudioMixer(TRUE)
{ 
  mono = FALSE; 
  started = FALSE; 
}

BOOL OpalRecordManager::Mixer_T::Open(const PFilePath & fn)
{
  PWaitAndSignal m(mutex);

  if (!started) {
    file.SetFormat(OpalWAVFile::fmt_PCM);
    file.Open(fn, PFile::ReadWrite);
    if (!mono)
      file.SetChannels(2);
    started = TRUE;
  }
  return TRUE;
}

BOOL OpalRecordManager::Mixer_T::Close()
{
  PWaitAndSignal m(mutex);
  file.Close();
  return TRUE;
}

BOOL OpalRecordManager::Mixer_T::OnWriteAudio(const MixerFrame & mixerFrame)
{
  if (file.IsOpen()) {
    OpalAudioMixerStream::StreamFrame frame;
    if (mono) {
      mixerFrame.GetMixedFrame(frame);
      file.Write(frame.GetPointerAndLock(), frame.GetSize());
      frame.Unlock();
    } else {
      mixerFrame.GetStereoFrame(frame);
      file.Write(frame.GetPointerAndLock(), frame.GetSize());
      frame.Unlock();
    }
  }
  return TRUE;
}

OpalRecordManager::OpalRecordManager()
{
  started = FALSE;
}

BOOL OpalRecordManager::Open(const PString & _callToken, const PFilePath & fn)
{
  PWaitAndSignal m(mutex);

  if (_callToken.IsEmpty())
    return FALSE;

  if (token.IsEmpty())
    token = _callToken;
  else if (_callToken != token)
    return FALSE;

  return mixer.Open(fn);
}

BOOL OpalRecordManager::CloseStream(const PString & _callToken, const std::string & _strm)
{
  {
    PWaitAndSignal m(mutex);
    if (_callToken.IsEmpty() || token.IsEmpty() || (token != _callToken))
      return FALSE;

    mixer.RemoveStream(_strm);
  }
  return TRUE;
}

BOOL OpalRecordManager::Close(const PString & _callToken)
{
  {
    PWaitAndSignal m(mutex);
    if (_callToken.IsEmpty() || token.IsEmpty() || (token != _callToken))
      return FALSE;

    mixer.RemoveAllStreams();
  }
  mixer.Close();
  return TRUE;
}

BOOL OpalRecordManager::WriteAudio(const PString & _callToken, const std::string & strm, const RTP_DataFrame & rtp)
{ 
  PWaitAndSignal m(mutex);
  if (_callToken.IsEmpty() || token.IsEmpty() || (token != _callToken))
    return FALSE;

  return mixer.Write(strm, rtp);
}
