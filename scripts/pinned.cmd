rem The pinned toolchain. Changing any of these changes the C runtime's sinf and must be re-verified:
rem the generator refuses to run unless its sinf fingerprint matches (generator\cloudnoise_gen.cpp).
set PINNED_VCVARS_VER=14.51
set PINNED_WINSDK=10.0.26100.0
