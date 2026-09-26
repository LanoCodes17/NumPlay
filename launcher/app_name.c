/* The app's name on the calculator's home screen. The Makefile builds NumPlay
 * under a few names and icons (VARIANTS); everything else is the same. */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = NP_APP_NAME;
