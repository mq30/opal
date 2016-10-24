/*
 * lyncshim.h
 *
 * Header file for Lync (UCMA) interface
 *
 * Copyright (C) 2016 Vox Lucida Pty. Ltd.
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
 * The Original Code is Portable Windows Library.
 *
 * The Initial Developer of the Original Code is Equivalence Pty. Ltd.
 *
 * Contributor(s): ______________________________________.
 */

#include <opal_config.h>

#if OPAL_LYNC

/// Class to hide Lync UCMA Managed code
class OpalLyncSim
{
public:
  OpalLyncSim();
  ~OpalLyncSim();

private:
  struct Impl;
  Impl * m_impl;
};

#endif // OPAL_LYNC

// End of File /////////////////////////////////////////////////////////////