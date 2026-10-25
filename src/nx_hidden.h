/* Forced into every lifted chunk on the Switch (CMakeLists.txt): the NRO is
 * one static image, so all symbols are local to it. Hidden visibility lets
 * the compiler address globals PC-relative instead of through the GOT. */
#pragma GCC visibility push(hidden)
