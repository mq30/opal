/*
 * echocancel.cxx
 *
 * Open Phone Abstraction Library (OPAL)
 * Formally known as the Open H323 project.
 *
 * Copyright (c) 2001 Post Increment
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
 * The author of this code is Damien Sandras
 *
 * Contributor(s): Miguel Rodriguez Perez.
 *
 */

#include <ptlib.h>

#ifdef __GNUC__
#pragma implementation "echocancel.h"
#endif

#include <opal_config.h>

#if OPAL_AEC

extern "C" {
  #ifdef OPAL_SYSTEM_SPEEX
    #include <speex/speex_echo.h>
    #include <speex/speex_preprocess.h>
  #else
    #include "speex/libspeex/speex_echo.h"
    #include "speex/libspeex/speex_preprocess.h"
  #endif
};

#include <codec/echocancel.h>


///////////////////////////////////////////////////////////////////////////////

OpalEchoCanceler::OpalEchoCanceler()
  : receiveHandler(PCREATE_NOTIFIER(ReceivedPacket))
  , sendHandler(PCREATE_NOTIFIER(SentPacket))
{
  echoState = NULL;
  preprocessState = NULL;

  e_buf = NULL;
  echo_buf = NULL;
  ref_buf = NULL;
  noise = NULL;

  echo_chan = new PQueueChannel();
  echo_chan->Open(10000);
  echo_chan->SetReadTimeout(10);
  echo_chan->SetWriteTimeout(10);

  mean = 0;
  clockRate = 8000;

  PTRACE(4, "Echo Canceler\tHandler created");
}


OpalEchoCanceler::~OpalEchoCanceler()
{
  PWaitAndSignal m(stateMutex);
  if (echoState) {
    speex_echo_state_destroy(echoState);
    echoState = NULL;
  }
  
  if (preprocessState) {
    speex_preprocess_state_destroy(preprocessState);
    preprocessState = NULL;
  }

  if (ref_buf)
    free(ref_buf);
  if (e_buf)
    free(e_buf);
  if (echo_buf)
    free(echo_buf);
  if (noise)
    free(noise);
  
  echo_chan->Close();
  delete(echo_chan);
}


void OpalEchoCanceler::SetParameters(const Params& newParam)
{
  PWaitAndSignal m(stateMutex);
  param = newParam;

  if (echoState) {
    speex_echo_state_destroy(echoState);
    echoState = NULL;
  }
  
  if (preprocessState) {
    speex_preprocess_state_destroy(preprocessState);
    preprocessState = NULL;
  }
}


void OpalEchoCanceler::SetClockRate(const int rate)
{
  clockRate = rate;
}


void OpalEchoCanceler::SentPacket(RTP_DataFrame& echo_frame, P_INT_PTR)
{
  /* Write to the soundcard, and write the frame to the PQueueChannel */
  if (param.m_enabled && echo_frame.GetPayloadSize() > 0)
    echo_chan->Write(echo_frame.GetPayloadPtr(), echo_frame.GetPayloadSize());
}


void OpalEchoCanceler::ReceivedPacket(RTP_DataFrame& input_frame, P_INT_PTR)
{
  if (!param.m_enabled || input_frame.GetPayloadSize() == 0)
    return;

  size_t inputSize = input_frame.GetPayloadSize(); // Size is in bytes

  PWaitAndSignal m(stateMutex);

  if (echoState == NULL) 
    echoState = speex_echo_state_init(inputSize/sizeof(short), param.m_duration);

  if (preprocessState == NULL) { 
    preprocessState = speex_preprocess_state_init(inputSize/sizeof(short), clockRate);
    int dummy = 0;
    speex_preprocess_ctl(preprocessState, SPEEX_PREPROCESS_SET_DENOISE, &dummy);
  }

  if (echo_buf == NULL)
    echo_buf = (spx_int16_t *) malloc(inputSize);
  if (noise == NULL)
#if OPAL_SPEEX_FLOAT_NOISE
    noise = malloc((inputSize/sizeof(short)+1)*sizeof(float));
#else
    noise = malloc((inputSize/sizeof(short)+1)*sizeof(spx_int32_t));
#endif
  if (e_buf == NULL)
    e_buf = (spx_int16_t *) malloc(inputSize);
  if (ref_buf == NULL)
    ref_buf = (spx_int16_t *) malloc(inputSize);

  /* Remove the DC offset */
  short *j = (short *) input_frame.GetPayloadPtr();
  for (size_t i = 0 ; i < inputSize/sizeof(short) ; i++) {
    mean = 0.999*mean + 0.001*j[i];
    ((spx_int16_t *)ref_buf)[i] = j[i] - (short) mean;
  }
  
  /* Read from the PQueueChannel a reference echo frame of the size
   * of the captured frame. */
  if (!echo_chan->Read((short *) echo_buf, input_frame.GetPayloadSize())) {
    
    /* Nothing to read from the speaker signal, only suppress the noise
     * and return.
     */
    speex_preprocess(preprocessState, (spx_int16_t *)ref_buf, NULL);
    memcpy(input_frame.GetPayloadPtr(), (spx_int16_t *)ref_buf, input_frame.GetPayloadSize());

    return;
  }
   
  /* Cancel the echo in this frame */
#if OPAL_SPEEX_FLOAT_NOISE
  speex_echo_cancel(echoState, (short *)ref_buf, (short *)echo_buf, (short *)e_buf, (float *)noise);
#else
  speex_echo_cancel(echoState, (short *)ref_buf, (short *)echo_buf, (short *)e_buf, (spx_int32_t *)noise);
#endif
  
  /* Suppress the noise */
#if OPAL_SPEEX_FLOAT_NOISE
  speex_preprocess(preprocessState, (spx_int16_t *)e_buf, (float *)noise);
#else
  speex_preprocess(preprocessState, (spx_int16_t *)e_buf, (spx_int32_t *)noise);
#endif

  /* Use the result of the echo cancelation as capture frame */
  memcpy(input_frame.GetPayloadPtr(), e_buf, input_frame.GetPayloadSize());
}


#endif // OPAL_AEC
