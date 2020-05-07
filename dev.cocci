// Invoke:
// spatch --sp-file dev.cocci --dir . --very-quiet --include-headers
// Options: --include-headers

@@
expression E;
identifier F =~ "^(name|init|device_pm_control|pm|config_info)$";
@@
 E->
-config->
 F
