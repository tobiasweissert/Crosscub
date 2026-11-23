struct SnapshotFault {bool armed=false;size_t baseline=0;int kind=0;unsigned failures=0;};
static void* snapshotAlloc(void* user,void* ptr,size_t oldSize,size_t size){
 auto& fault=*static_cast<SnapshotFault*>(user);
 if(!size){free(ptr);return nullptr;}
 if(fault.armed && !ptr && (fault.failures || (nativeLiveBytes>fault.baseline+8192 && int(oldSize)==fault.kind))){++fault.failures;return nullptr;}
 return realloc(ptr,size);
}
static void snapshotLifetime(const std::string& mode){
 SnapshotFault fault;lua_State* L=lua_newstate(snapshotAlloc,&fault);assert(L);assert(ink::runtime::openEssential(L)==LUA_OK);registerAdapter(L);
 code(L,"snapshot_text=string.rep('x',8192);function good_snapshot() return smudge.keyboard_snapshot('probe',snapshot_text,1,false,false,1) end;function bad_snapshot() return smudge.keyboard_snapshot('probe',snapshot_text,1,false,false,1,false,0) end");
 // Warm native fonts/settings/layout caches before measuring ownership.
 code(L,"good_snapshot();collectgarbage('collect')");
 if(mode=="snapshot-invalid"){
  const auto before=nativeLiveBytes;
  code(L,"for i=1,128 do assert(not pcall(bad_snapshot)) end;collectgarbage('collect')");
  printf("SNAPSHOT_INVALID native_before=%zu native_after=%zu calls=128 text_bytes=8192\n",before,nativeLiveBytes);fflush(stdout);assert(before==nativeLiveBytes);
 }else{
  for(int kind:{LUA_TTABLE,LUA_TSTRING}){
   lua_gc(L,LUA_GCCOLLECT,0);const auto before=nativeLiveBytes;fault.baseline=before;fault.kind=kind;fault.failures=0;
   lua_getglobal(L,"good_snapshot");fault.armed=true;int status=lua_pcall(L,0,1,0);fault.armed=false;
   printf("SNAPSHOT_OOM kind=%d status=%d fault_count=%u native_before=%zu native_after=%zu\n",kind,status,fault.failures,before,nativeLiveBytes);fflush(stdout);
   assert(status==LUA_ERRMEM && fault.failures>0);lua_pop(L,1);assert(before==nativeLiveBytes);
   code(L,"assert(type(good_snapshot())=='table');collectgarbage('collect')");assert(before==nativeLiveBytes);
  }
 }
 lua_close(L);puts("SNAPSHOT_LIFETIME_PASS actual_C_Lua native_allocation_balance recovery");
}
