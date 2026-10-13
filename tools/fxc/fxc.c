#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
typedef struct ID3DBlobVtbl { void* q; ULONG (__stdcall*AddRef)(void*); ULONG (__stdcall*Release)(void*); void* (__stdcall*GetBufferPointer)(void*); SIZE_T (__stdcall*GetBufferSize)(void*);} ID3DBlobVtbl;
typedef struct { ID3DBlobVtbl* v; } Blob;
typedef HRESULT (WINAPI *PD3DCompile)(const void*,SIZE_T,const char*,const void*,void*,const char*,const char*,UINT,UINT,Blob**,Blob**);
int main(int argc,char**argv){ /* fxc in.hlsl entry profile out.bin */
 if(argc<5){printf("usage\n");return 2;}
 HMODULE m=LoadLibraryA("d3dcompiler_47.dll"); if(!m){printf("no d3dcompiler\n");return 3;}
 PD3DCompile c=(PD3DCompile)GetProcAddress(m,"D3DCompile");
 FILE*f=fopen(argv[1],"rb"); fseek(f,0,2); long n=ftell(f); fseek(f,0,0); char*s=malloc(n+1); fread(s,1,n,f); s[n]=0; fclose(f);
 Blob*code=0,*err=0; HRESULT hr=c(s,n,argv[1],0,0,argv[2],argv[3],(1<<15)/*OPTIMIZATION_LEVEL3*/,0,&code,&err);
 if(err){ printf("%s\n",(char*)err->v->GetBufferPointer(err)); }
 if(FAILED(hr)){ printf("compile failed %08lx\n",hr); return 1;}
 f=fopen(argv[4],"wb"); fwrite(code->v->GetBufferPointer(code),1,code->v->GetBufferSize(code),f); fclose(f);
 printf("ok %u bytes\n",(unsigned)code->v->GetBufferSize(code)); return 0; }
