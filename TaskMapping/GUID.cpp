unsigned generate_GUID() {
    static unsigned GUID = 0;
    return GUID++;
}