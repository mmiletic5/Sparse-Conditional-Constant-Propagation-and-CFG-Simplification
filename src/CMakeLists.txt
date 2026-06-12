add_llvm_library( LLVMOurPass MODULE
  OurSCCPPass.cpp
  
  PLUGIN_TOOL
  opt
  )
