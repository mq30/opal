//------------------------------------------------------------------------------
// <auto-generated />
//
// This file was automatically generated by SWIG (http://www.swig.org).
// Version 3.0.7
//
// Do not make changes to this file unless you know what you are doing--modify
// the SWIG interface file instead.
//------------------------------------------------------------------------------


public class OpalStatusLineAppearance : global::System.IDisposable {
  private global::System.Runtime.InteropServices.HandleRef swigCPtr;
  protected bool swigCMemOwn;

  internal OpalStatusLineAppearance(global::System.IntPtr cPtr, bool cMemoryOwn) {
    swigCMemOwn = cMemoryOwn;
    swigCPtr = new global::System.Runtime.InteropServices.HandleRef(this, cPtr);
  }

  internal static global::System.Runtime.InteropServices.HandleRef getCPtr(OpalStatusLineAppearance obj) {
    return (obj == null) ? new global::System.Runtime.InteropServices.HandleRef(null, global::System.IntPtr.Zero) : obj.swigCPtr;
  }

  ~OpalStatusLineAppearance() {
    Dispose();
  }

  public virtual void Dispose() {
    lock(this) {
      if (swigCPtr.Handle != global::System.IntPtr.Zero) {
        if (swigCMemOwn) {
          swigCMemOwn = false;
          OPALPINVOKE.delete_OpalStatusLineAppearance(swigCPtr);
        }
        swigCPtr = new global::System.Runtime.InteropServices.HandleRef(null, global::System.IntPtr.Zero);
      }
      global::System.GC.SuppressFinalize(this);
    }
  }

  public string line {
    set {
      OPALPINVOKE.OpalStatusLineAppearance_line_set(swigCPtr, value);
    } 
    get {
      string ret = OPALPINVOKE.OpalStatusLineAppearance_line_get(swigCPtr);
      return ret;
    } 
  }

  public OpalLineAppearanceStates state {
    set {
      OPALPINVOKE.OpalStatusLineAppearance_state_set(swigCPtr, (int)value);
    } 
    get {
      OpalLineAppearanceStates ret = (OpalLineAppearanceStates)OPALPINVOKE.OpalStatusLineAppearance_state_get(swigCPtr);
      return ret;
    } 
  }

  public int appearance {
    set {
      OPALPINVOKE.OpalStatusLineAppearance_appearance_set(swigCPtr, value);
    } 
    get {
      int ret = OPALPINVOKE.OpalStatusLineAppearance_appearance_get(swigCPtr);
      return ret;
    } 
  }

  public string callId {
    set {
      OPALPINVOKE.OpalStatusLineAppearance_callId_set(swigCPtr, value);
    } 
    get {
      string ret = OPALPINVOKE.OpalStatusLineAppearance_callId_get(swigCPtr);
      return ret;
    } 
  }

  public string partyA {
    set {
      OPALPINVOKE.OpalStatusLineAppearance_partyA_set(swigCPtr, value);
    } 
    get {
      string ret = OPALPINVOKE.OpalStatusLineAppearance_partyA_get(swigCPtr);
      return ret;
    } 
  }

  public string partyB {
    set {
      OPALPINVOKE.OpalStatusLineAppearance_partyB_set(swigCPtr, value);
    } 
    get {
      string ret = OPALPINVOKE.OpalStatusLineAppearance_partyB_get(swigCPtr);
      return ret;
    } 
  }

  public OpalStatusLineAppearance() : this(OPALPINVOKE.new_OpalStatusLineAppearance(), true) {
  }

}
