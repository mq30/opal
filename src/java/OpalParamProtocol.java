/* ----------------------------------------------------------------------------
 * This file was automatically generated by SWIG (http://www.swig.org).
 * Version 3.0.7
 *
 * Do not make changes to this file unless you know what you are doing--modify
 * the SWIG interface file instead.
 * ----------------------------------------------------------------------------- */

package org.opalvoip.opal;

public class OpalParamProtocol {
  private transient long swigCPtr;
  protected transient boolean swigCMemOwn;

  protected OpalParamProtocol(long cPtr, boolean cMemoryOwn) {
    swigCMemOwn = cMemoryOwn;
    swigCPtr = cPtr;
  }

  protected static long getCPtr(OpalParamProtocol obj) {
    return (obj == null) ? 0 : obj.swigCPtr;
  }

  protected void finalize() {
    delete();
  }

  public synchronized void delete() {
    if (swigCPtr != 0) {
      if (swigCMemOwn) {
        swigCMemOwn = false;
        OPALJNI.delete_OpalParamProtocol(swigCPtr);
      }
      swigCPtr = 0;
    }
  }

  public void setPrefix(String value) {
    OPALJNI.OpalParamProtocol_prefix_set(swigCPtr, this, value);
  }

  public String getPrefix() {
    return OPALJNI.OpalParamProtocol_prefix_get(swigCPtr, this);
  }

  public void setUserName(String value) {
    OPALJNI.OpalParamProtocol_userName_set(swigCPtr, this, value);
  }

  public String getUserName() {
    return OPALJNI.OpalParamProtocol_userName_get(swigCPtr, this);
  }

  public void setDisplayName(String value) {
    OPALJNI.OpalParamProtocol_displayName_set(swigCPtr, this, value);
  }

  public String getDisplayName() {
    return OPALJNI.OpalParamProtocol_displayName_get(swigCPtr, this);
  }

  public void setProduct(OpalProductDescription value) {
    OPALJNI.OpalParamProtocol_product_set(swigCPtr, this, OpalProductDescription.getCPtr(value), value);
  }

  public OpalProductDescription getProduct() {
    long cPtr = OPALJNI.OpalParamProtocol_product_get(swigCPtr, this);
    return (cPtr == 0) ? null : new OpalProductDescription(cPtr, false);
  }

  public void setInterfaceAddresses(String value) {
    OPALJNI.OpalParamProtocol_interfaceAddresses_set(swigCPtr, this, value);
  }

  public String getInterfaceAddresses() {
    return OPALJNI.OpalParamProtocol_interfaceAddresses_get(swigCPtr, this);
  }

  public void setUserInputMode(OpalUserInputModes value) {
    OPALJNI.OpalParamProtocol_userInputMode_set(swigCPtr, this, value.swigValue());
  }

  public OpalUserInputModes getUserInputMode() {
    return OpalUserInputModes.swigToEnum(OPALJNI.OpalParamProtocol_userInputMode_get(swigCPtr, this));
  }

  public void setDefaultOptions(String value) {
    OPALJNI.OpalParamProtocol_defaultOptions_set(swigCPtr, this, value);
  }

  public String getDefaultOptions() {
    return OPALJNI.OpalParamProtocol_defaultOptions_get(swigCPtr, this);
  }

  public OpalParamProtocol() {
    this(OPALJNI.new_OpalParamProtocol(), true);
  }

}
