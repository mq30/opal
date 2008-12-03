/*
 * msrp.h
 *
 * Support for RFC 4975 Message Session Relay Protocol (MSRP)
 *
 * Open Phone Abstraction Library (OPAL)
 *
 * Copyright (c) 2008 Post Increment
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
 * The Initial Developer of the Original Code is Post Increment
 *
 * Contributor(s): ______________________________________.
 *
 * $Revision: 21293 $
 * $Author: rjongbloed $
 * $Date: 2008-10-13 10:24:41 +1100 (Mon, 13 Oct 2008) $
 */

#include <ptlib.h>
#include <opal/buildopts.h>

#ifdef __GNUC__
#pragma implementation "msrp.h"
#endif

#include <ptlib/socket.h>
#include <ptclib/random.h>

#include <opal/transports.h>
#include <opal/mediatype.h>
#include <opal/mediafmt.h>

#include <im/msrp.h>

#define DEFAULT_MSRP_PORT   2855

#if OPAL_IM_CAPABILITY

#if OPAL_SIP


/////////////////////////////////////////////////////////
//
//  SDP media description for MSRP
//

class SDPMSRPMediaDescription : public SDPMediaDescription
{
  PCLASSINFO(SDPMSRPMediaDescription, SDPMediaDescription);
  public:
    SDPMSRPMediaDescription(const OpalTransportAddress & address, const PString & url);
    virtual PCaselessString GetSDPTransportType() const;
    virtual SDPMediaFormat * CreateSDPMediaFormat(const PString & portString);
    virtual PString GetSDPMediaType() const;
    virtual PString GetSDPPortList() const;
    virtual bool PrintOn(ostream & str, const PString & connectString) const;
    virtual void SetAttribute(const PString & attr, const PString & value);
    virtual void ProcessMediaOptions(SDPMediaFormat & sdpFormat, const OpalMediaFormat & mediaFormat);
    virtual void AddMediaFormat(const OpalMediaFormat & mediaFormat);

  protected:
    PString url;
    PStringToString msrpAttributes;
};

class SDPMSRPMediaFormat : public SDPMediaFormat
{
  public:
    SDPMSRPMediaFormat()
      : SDPMediaFormat("im", RTP_DataFrame::MaxPayloadType)
    { }
};

////////////////////////////////////////////////////////////////////////////////////////////

SDPMediaDescription * OpalIMMediaType::CreateSDPMediaDescription(const OpalTransportAddress & /*localAddress*/)
{
  return NULL;
}


///////////////////////////////////////////////////////////////////////////////////////////

SDPMSRPMediaDescription::SDPMSRPMediaDescription(const OpalTransportAddress & address, const PString & _url)
  : SDPMediaDescription(address), url(_url)
{
}

PCaselessString SDPMSRPMediaDescription::GetSDPTransportType() const
{ 
  return "TCP/MSRP";
}

PString SDPMSRPMediaDescription::GetSDPMediaType() const 
{ 
  return "message"; 
}

SDPMediaFormat * SDPMSRPMediaDescription::CreateSDPMediaFormat(const PString & /*portString*/)
{
  return new SDPMSRPMediaFormat();
}

PString SDPMSRPMediaDescription::GetSDPPortList() const
{
  return " *";
}

bool SDPMSRPMediaDescription::PrintOn(ostream & str, const PString & /*connectString*/) const
{
  // call ancestor. Never output the connect string, as the listening TCP sockets 
  // for the MSRP manager will always give an address of 0.0.0.0
  if (!SDPMediaDescription::PrintOn(str, ""))
    return false;

  PINDEX i;

  str << "a=accept-types:";
  for (i = 0; i < formats.GetSize(); ++i) {
    if (i != 0)
      str << " ";
    str << formats[i].GetEncodingName();
  }
  str << "\r\n";

  str << "a=path:" << url << "\r\n";

  // output options
  for (i = 0; i < msrpAttributes.GetSize(); i++) 
    str << "a=" << msrpAttributes.GetKeyAt(i) << ":" << msrpAttributes.GetDataAt(i) << "\r\n";

  return true;
}

void SDPMSRPMediaDescription::SetAttribute(const PString & attr, const PString & value)
{
  if (attr.Left(5) *= "msrp-") {
    msrpAttributes.SetAt(attr.Mid(5), value);
    return;
  }
}

void SDPMSRPMediaDescription::ProcessMediaOptions(SDPMediaFormat & /*sdpFormat*/, const OpalMediaFormat & mediaFormat)
{
  if (mediaFormat.GetMediaType() == OpalMediaType::Fax()) {
    for (PINDEX i = 0; i < mediaFormat.GetOptionCount(); ++i) {
      const OpalMediaOption & option = mediaFormat.GetOption(i);
      if (option.GetName().Left(5) *= "msrp-") 
        msrpAttributes.SetAt(option.GetName().Mid(5), option.AsString());
    }
  }
}

void SDPMSRPMediaDescription::AddMediaFormat(const OpalMediaFormat & mediaFormat)
{
  if (!mediaFormat.IsTransportable() || !mediaFormat.IsValidForProtocol("sip") || mediaFormat.GetMediaType() != "im") {
    PTRACE(4, "MSRP\tSDP not including " << mediaFormat << " as it is not a valid MSRP format");
    return;
  }

  for (SDPMediaFormatList::iterator format = formats.begin(); format != formats.end(); ++format) {
    if (format->GetMediaFormat().GetMediaType() != "im")
      continue;
    if (format->GetEncodingName() *= mediaFormat.GetEncodingName()) {
      PTRACE(4, "SDP\tSDP not including " << mediaFormat << " as it is already included");
      return;
    }
  }

  SDPMediaFormat * sdpFormat = new SDPMediaFormat(mediaFormat, mediaFormat.GetPayloadType());

  ProcessMediaOptions(*sdpFormat, mediaFormat);

  AddSDPMediaFormat(sdpFormat);
}


#endif // OPAL_SIP

////////////////////////////////////////////////////////////////////////////////////////////

MSRPSession::MSRPSession()
{
  // sessionIDs are supposed to unguessable
  msrpSessionId = OpalMSRPManager::Current().AllocateID();

  WORD port;
  PAssert(OpalMSRPManager::Current().GetLocalPort(port), "Cannot start MSRP manager");
  PString hostname = PIPSocket::GetHostName();

  PStringStream str;
  str << "msrp://"
      << hostname
      << ":"
      << port
      << "/"
      << msrpSessionId
      << ";tcp";

  url = str;
}

MSRPSession::~MSRPSession()
{
  OpalMSRPManager::Current().DeallocateID(msrpSessionId);
}

SDPMediaDescription * MSRPSession::CreateSDPMediaDescription(const OpalTransportAddress & sdpContactAddress)
{
  return new SDPMSRPMediaDescription(sdpContactAddress, url);
}


////////////////////////////////////////////////////////////////////////////////////////////

OpalMSRPMediaSession::OpalMSRPMediaSession(OpalConnection & _conn, unsigned /*sessionId*/)
: OpalMediaSession(_conn, "im")
{
  msrpSession = new MSRPSession();
}

OpalMSRPMediaSession::OpalMSRPMediaSession(const OpalMSRPMediaSession & _obj)
  : OpalMediaSession(_obj)
  , msrpSession(_obj.msrpSession)
{
}

void OpalMSRPMediaSession::Close()
{
  if (msrpSession != NULL) {
    delete msrpSession;
    msrpSession = NULL;
  }
}

OpalTransportAddress OpalMSRPMediaSession::GetLocalMediaAddress() const
{
  OpalTransportAddress addr;
  if (OpalMSRPManager::Current().GetLocalAddress(addr))
    return addr;

  return OpalTransportAddress();
}

SDPMediaDescription * OpalMSRPMediaSession::CreateSDPMediaDescription(const OpalTransportAddress & sdpContactAddress)
{
  return msrpSession->CreateSDPMediaDescription(sdpContactAddress);
}

////////////////////////////////////////////////////////////////////////////////////////////

OpalMSRPManager & OpalMSRPManager::Current()
{
  static PMutex mutex;

  PWaitAndSignal m(mutex);

  static OpalMSRPManager msrp;
  return msrp;
}

OpalMSRPManager::OpalMSRPManager()
  : listeningThread(NULL)
{
}

OpalMSRPManager::~OpalMSRPManager()
{
  PWaitAndSignal m(mutex);

  if (listeningThread != NULL) {
    listeningSocket.Close();
    listeningThread->WaitForTermination();
    delete listeningThread;
  }
}

std::string OpalMSRPManager::AllocateID()
{
  PWaitAndSignal m(mutex);

  std::string id = psprintf("%c%08x%u", PRandom::Number('a', 'z'), PRandom::Number(), ++lastID);
  SessionInfo sessionInfo;
  sessionInfoMap.insert(SessionInfoMap::value_type(id, sessionInfo));

  return id;
}

void OpalMSRPManager::DeallocateID(const std::string & id)
{
  PWaitAndSignal m(mutex);

  SessionInfoMap::iterator r = sessionInfoMap.find(id);
  if (r != sessionInfoMap.end())
    sessionInfoMap.erase(r);
}

bool OpalMSRPManager::GetLocalAddress(OpalTransportAddress & addr)
{
  PWaitAndSignal m(mutex);

  if (!listeningSocket.IsOpen()) {
    if (!listeningSocket.Listen(5, DEFAULT_MSRP_PORT)) {
      PTRACE(2, "MSRP\tCannot start MSRP listened on port " << DEFAULT_MSRP_PORT);
      return false;
    }

    listeningThread = new PThreadObj<OpalMSRPManager>(*this, &OpalMSRPManager::ThreadMain);
  }

  PIPSocket::Address ip; WORD port;
  listeningSocket.GetLocalAddress(ip, port);
  addr = OpalTransportAddress(ip, port);
  return true;
}

bool OpalMSRPManager::GetLocalPort(WORD & port)
{
  OpalTransportAddress addr;
  if (!GetLocalAddress(addr))
    return false;
  PIPSocket::Address ip;
  return addr.GetIpAndPort(ip, port);
}

void OpalMSRPManager::ThreadMain()
{
  PTRACE(2, "MSRP\tListener thread started");
  for (;;) {
    PTCPSocket * socket = new PTCPSocket;
    if (!socket->Accept(listeningSocket)) {
      delete socket;
      break;
    }
  }
  PTRACE(2, "MSRP\tListener thread ended");
}

#endif //  OPAL_IM_CAPABILITY



