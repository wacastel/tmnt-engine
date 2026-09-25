extern struct BurnDriver BurnDrvTmnt;
static struct BurnDriver* pDriver[] = { &BurnDrvTmnt };
static struct { const char *game_name; char *sourcefile; } sourcefile_table[] = {{"tmnt",(char*)"konami/d_tmnt.cpp"},{"",nullptr}};
