add_llvm_library( LLVMOurPass MODULE
  OurSCCPPass.cpp
  OurSimplifyCFGPass.cpp
  
  PLUGIN_TOOL
  opt
  )
