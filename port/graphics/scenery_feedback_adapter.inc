static D2SceneryFeedback d2_scenery_feedback[D2_SCENERY_HISTORY_LIMIT];
static unsigned d2_scenery_feedback_count,d2_scenery_feedback_age;
static int d2_scenery_feedback_loaded,d2_scenery_feedback_cache_valid;
static void d2_scenery_feedback_load(void){
    if(d2_scenery_feedback_loaded)return;d2_scenery_feedback_loaded=1;
    const char* path=getenv("D2_SCENERY_FEEDBACK_FILE");if(!path||!*path)return;
    FILE* file=d2_capture_fopen(path,"rb");if(!file)return;
    char magic[8];u32 count=0;u64 checksum=0;
    D2SceneryFeedback records[D2_SCENERY_HISTORY_LIMIT]={0};
    int valid=fread(magic,1,8,file)==8 && !memcmp(magic,"D2CFv001",8) &&
        fread(&count,sizeof(count),1,file)==1 && count<=D2_SCENERY_HISTORY_LIMIT &&
        fread(&checksum,sizeof(checksum),1,file)==1 && fread(records,sizeof(D2SceneryFeedback),count,file)==count;
    if(valid)valid=fgetc(file)==EOF && d2_scenery_feedback_valid(records,count) &&
        checksum==d2_scenery_feedback_checksum(records,count*sizeof(D2SceneryFeedback));
    fclose(file);if(!valid)return;
    memcpy(d2_scenery_feedback,records,count*sizeof(D2SceneryFeedback));d2_scenery_feedback_count=count;
    for(unsigned i=0;i<count;i++)if(records[i].age>d2_scenery_feedback_age)d2_scenery_feedback_age=records[i].age;
    d2_scenery_feedback_cache_valid=1;
}
static void d2_scenery_feedback_save(void){
    const char* path=getenv("D2_SCENERY_FEEDBACK_FILE");if(!path||!*path)return;
    FILE* file=d2_capture_fopen(path,"wb");if(!file)return;
    u32 count=d2_scenery_feedback_count;u64 checksum=d2_scenery_feedback_checksum(d2_scenery_feedback,count*sizeof(D2SceneryFeedback));
    fwrite("D2CFv001",1,8,file);fwrite(&count,sizeof(count),1,file);fwrite(&checksum,sizeof(checksum),1,file);
    fwrite(d2_scenery_feedback,sizeof(D2SceneryFeedback),count,file);fclose(file);
}
