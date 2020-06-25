/** Solver modeling
    Enable modeling of solution to improve performance
 **/

#include "gridlabd.h"

#include <stdlib.h>
#include <unistd.h>

#include "solver_py.h"

#undef Py_INCREF
// #define Py_INCREF(X) (fprintf(stderr,"Py_INCREF(" #X "=<%p>",X),PyObject_Print(X,stderr,Py_PRINT_RAW),fprintf(stderr,") --> %d\n",(int)++X->ob_refcnt),X->ob_refcnt)
#define Py_INCREF(X) (X->ob_refcnt++)
#undef Py_DECREF
#define Py_DECREF(X) (X->ob_refcnt--)
// #define Py_DECREF(X) (fprintf(stderr,"Py_DECREF(" #X "=<%p>",X),PyObject_Print(X,stderr,Py_PRINT_RAW),fprintf(stderr,") --> %d\n",(int)--X->ob_refcnt),X->ob_refcnt)

#define CONFIGNAME "solver_py.conf"
#define CONFIGPATH "/usr/local/var/gridlabd/"

static SOLVERPYTHONSTATUS solver_py_status = SPS_INIT;
char1024 solver_py_config = CONFIGPATH CONFIGNAME;
static const char *model_busdump = NULL;
static const char *model_branchdump = NULL;
static const char *model_dump_handler = NULL;
static const char *module_import_path = NULL; // path to use when importing modules
static const char *module_import_name = NULL; // module name to import (python only)
static PyObject *pModule = NULL;
static int solver_python_loglevel = 0; // -1=disable, 0 = minimal ... 9 = everything,
static FILE *solver_python_logfh = NULL;
static const char *python_busdata = "type,phases,"
	"volt_base,mva_base,origphases,"
	"SAr,SAi,SBr,SBi,SCr,SCi,"
	"YAr,YAi,YBr,YBi,YCr,YCi,"
	"IAr,IAi,IBr,IBi,ICr,ICi,"
	"prerot_IAr,prerot_IAi,prerot_IBr,prerot_IBi,prerot_ICr,prerot_ICi,"
	"S_dyAr,S_dyAi,S_dyBr,S_dyBi,S_dyCr,S_dyCi,"
	"Y_dyAr,Y_dyAi,Y_dyBr,Y_dyBi,Y_dyCr,Y_dyCi,"
	"I_dyAr,I_dyAi,I_dyBr,I_dyBi,I_dyCr,I_dyCi,"
	"PGenTotalAr,PGenTotalAi,PGenTotalBr,PGenTotalBi,PGenTotalCr,PGenTotalCi,"
	"DynCurrentAr,DynCurrentAi,DynCurrentBr,DynCurrentBi,DynCurrentCr,DynCurrentCi,"
	"PLA,QLA,PLB,QLB,PLC,QLC,"
	"full_YAAr,full_YAAi,full_YABr,full_YABi,full_YACr,full_YACi,full_YBBr,full_YBBi,full_YBCr,full_YBCi,full_YCCr,full_YCCi,"
	"full_Y_allAAr,full_Y_allAAi,full_Y_allABr,full_Y_allABi,full_Y_allACr,full_Y_allACi,full_Y_allBBr,full_Y_allBBi,full_Y_allBCr,full_Y_allBCi,full_Y_allCCr,full_Y_allCCi,"
	"full_Y_loadAr,full_Y_loadAi,full_Y_loadBr,full_Y_loadBi,full_Y_loadCr,full_Y_loadCi,"
	"Jacob_A0,Jacob_A1,Jacob_A2,Jacob_B0,Jacob_B1,Jacob_B2,Jacob_C0,Jacob_C1,Jacob_C2,Jacob_D0,Jacob_D1,Jacob_D2";
static const char *python_branchdata = "phases,from,to,"
	"origphases,faultphases,lnk_type,fault_link_below,v_ratio,"
	"YfromAr,YfromAi,YfromBr,YfromBi,YfromCr,YfromCi,"
	"YtoAr,YtoAi,YtoBr,YtoBi,YtoCr,YtoCi,"
	"YSfromAr,YSfromAi,YSfromBr,YSfromBi,YSfromCr,YSfromCi,"
	"YStoAr,YStoAi,YStoBr,YStoBi,YStoCr,YStoCi";
static const char *python_learndata = "";
static const char *python_busid = NULL;
static const char *python_branchid = NULL;
static size_t python_nbustags = 0;
static const char **python_bustags = NULL;
static size_t python_nbranchtags = 0;
static const char **python_branchtags = NULL;
static size_t python_nlearntags = 0;
static const char **python_learntags = NULL;
static PyObject *pModel = NULL;
static PyObject *pSolution = NULL;
static PyObject *pBustags = NULL;
static PyObject *pBusdata = NULL;
static PyObject *pBranchtags = NULL;
static PyObject *pBranchdata = NULL;
static PyObject *pLearndata = NULL;
static PyObject *pKwargs = NULL;

FILE *SolverTimer::fp = NULL;

void solver_python_log(int level, const char *format, ...)
{
	if ( (int)level <= solver_python_loglevel && solver_python_logfh != NULL )
	{
		va_list ptr;
		va_start(ptr,format);
		vfprintf(solver_python_logfh,format,ptr);
		fprintf(solver_python_logfh,"\n");
		fflush(solver_python_logfh);
		va_end(ptr);
	}
}

void init_kwargs(void)
{
	if ( pKwargs == NULL )
	{
		pKwargs = PyDict_New();
		// Py_INCREF(pKwargs);
	}
}

SOLVERPYTHONSTATUS solver_python_config (
	const char *localconfig = NULL, 
	const char *shareconfig = CONFIGPATH CONFIGNAME)
{
	const char *configname = localconfig ? localconfig : (const char*)solver_py_config;
	FILE *fp = fopen(configname,"r");
	if ( fp == NULL )
	{
		configname = shareconfig;
		fp = fopen(configname,"r");
	}
	if ( fp != NULL )
	{
		SOLVERPYTHONSTATUS status = SPS_READY;
		char line[1024];
		while ( fgets(line,sizeof(line),fp) != NULL )
		{
			char tag[1024],value[1024];
			if ( sscanf(line,"%s%*[ \t]%[^\n]",tag,value) == 2 )
			{
				if ( tag[0] == '#' )
				{
					continue;
				}
				else if ( strcmp(tag,"logfile") == 0 )
				{
					solver_python_logfh = fopen(value,"w");
					if ( solver_python_logfh )
					{
						solver_python_log(0,"solver_python_config(configname='%s'): solver log '%s' opened ok",configname,value);
					}
					else
					{
						fprintf(stderr,"solver_python_config(configname='%s'): solver log '%s' opened failed (errno=%d, strerror='%s')\n",configname,value,errno,strerror(errno));
					}
				}
				else if ( strcmp(tag,"loglevel") == 0 )
				{
					solver_python_loglevel = atoi(value);
					solver_python_log(1,"solver_python_config(configname='%s'): solver_python_loglevel = %d",configname,solver_python_loglevel);
				}
				else if ( strcmp(tag,"busdump") == 0 )
				{
					model_busdump = strdup(value);
					solver_python_log(1,"solver_python_config(configname='%s'): model_busdump = '%s'",configname,model_busdump);
				}
				else if ( strcmp(tag,"branchdump") == 0 )
				{
					model_branchdump = strdup(value);
					solver_python_log(1,"solver_python_config(configname='%s'): model_branchdump = '%s'",configname,model_branchdump);
				}
				else if ( strcmp(tag,"on_dump") == 0 )
				{
					model_dump_handler = strdup(value);
					solver_python_log(1,"solver_python_config(configname='%s'): model_dump_handler = '%s'",configname,model_dump_handler);
				}
				else if ( strcmp(tag,"import") == 0 )
				{
					module_import_name = strdup(value);
					solver_python_log(1,"solver_python_config(configname='%s'): module_import_name = '%s'",configname,module_import_name);
				}
				else if ( strcmp(tag,"import_path") == 0 )
				{
					module_import_path = strdup(value);
					solver_python_log(1,"solver_python_config(configname='%s'): module_import_path = '%s'",configname,module_import_path);
				}
				else if ( strcmp(tag,"solver") == 0 )
				{
					if ( strcmp(value,"enable") == 0 )
					{
						status = SPS_READY;
						solver_python_log(1,"solver_python_config(configname='%s'): solver enabled",configname);
					}
					else if ( strcmp(value,"disable") == 0 )
					{
						status = SPS_DISABLED;
						solver_python_log(1,"solver_python_config(configname='%s'): solver disabled",configname);
					}
					else
					{
						fprintf(stderr,"solver_python_config(configname='%s'): tag '%s' value '%s' is invalid\n",configname,tag,value);
						status = SPS_FAILED;
					}
				}
				else if ( strcmp(tag,"busdata") == 0 )
				{
					python_busdata = strdup(value);
					solver_python_log(1,"solver_python_config(configname='%s'): python_busdata = '%s'",configname,python_busdata);					
				}
				else if ( strcmp(tag,"branchdata") == 0 )
				{
					python_branchdata = strdup(value);
					solver_python_log(1,"solver_python_config(configname='%s'): python_branchdata = '%s'",configname,python_branchdata);					
				}
				else if ( strcmp(tag,"learndata") == 0 )
				{
					python_learndata = strdup(value);
					solver_python_log(1,"solver_python_config(configname='%s'): python_learndata = '%s'",configname,python_learndata);					
				}
				else if ( strcmp(tag,"profiler") == 0 )
				{
					SolverTimer::open(value);
					solver_python_log(1,"solver_python_config(configname='%s'): profiler = '%s'",configname,value);					
				}
				else if ( strcmp(tag,"option") == 0 )
				{
					char lhs[1024], rhs[1024];
					if ( sscanf(value,"%[^=]=%[^\n]",lhs,rhs) == 2 )
					{
						PyObject *pValue = Py_BuildValue("z",rhs);
						// Py_INCREF(pValue);
						init_kwargs();
						if ( pValue && PyDict_SetItemString(pKwargs,lhs,pValue) != 0 )
						{
							fprintf(stderr,"solver_python_config(configname='%s'): option '%s' value '%s' is not valid\n",configname,lhs,rhs);
						}
						else
						{
							solver_python_log(1,"solver_python_config(configname='%s'): option %s=%s ok",configname,lhs,rhs);					
						}
					}
					else
					{
						fprintf(stderr,"solver_python_config(configname='%s'): 'option %s' is not a valid syntax\n",configname,value);
					}
				}
				else
				{
					fprintf(stderr,"solver_python_config(configname='%s'): tag '%s' is not valid\n",configname,tag);
				}
			}
		}
		fclose(fp);
		fflush(stderr);
		return status;
	}
	else
	{
		return SPS_DISABLED;
	}
}

char * strdup(const char *from, char delim, const char **next=NULL)
{
	size_t len = 0;
	for ( const char *p = from ; *p != delim ; p++)
	{
		len++;
	}
	if ( next )
	{
		*next = from + len + 1;
	}
	return strndup(from,len);
}

size_t strlen(const char *list, char delim)
{
	if ( *list == '\0' )
		return 0;
	size_t len = 1;
	while ( *list++ != '\0' )
	{
		if ( *list == delim )
			len++;
	}
	return len;
}

int strfind(size_t len, const char *list[], const char *item)
{
	for ( size_t n = 0 ; n < len ; n++ )
	{
		if ( strcmp(list[n],item) == 0 )
		{
			return n;
		}
	}
	return -1;
}

void init_bustags(void)
{
	const char *tags = python_busdata;
	if ( strchr(python_busdata,':') != NULL )
	{
		python_busid = strdup(python_busdata,':',&tags);
	}
	python_nbustags = strlen(tags,',');
	python_bustags = new const char*[python_nbustags];
	for ( size_t n = 0 ; n < python_nbustags ; n++ )
	{
		python_bustags[n] = strdup(tags,',',&tags);
	}
	if ( pBustags == NULL )
	{
		pBustags = PyDict_New();
		// Py_INCREF(pBustags);
	}
}

void init_branchtags(void)
{
	const char *tags = python_branchdata;
	if ( strchr(python_branchdata,':') != NULL )
	{
		python_branchid = strdup(python_branchdata,':',&tags);
	}
	python_nbranchtags = strlen(tags,',');
	python_branchtags = new const char*[python_nbranchtags];
	for ( size_t n = 0 ; n < python_nbranchtags ; n++ )
	{
		python_branchtags[n] = strdup(tags,',',&tags);
	}
	if ( pBranchtags == NULL )
	{	
		pBranchtags = PyDict_New();
		// Py_INCREF(pBranchtags);
	}
}

void init_busdata(void)
{
	if ( python_bustags == NULL )
	{
		init_bustags();
		if ( python_busid )
		{
			pBusdata = PyDict_New();
			solver_python_log(1,"init_busid(): bus id = %s", python_busid);
		}
		else
		{
			pBusdata = PyList_New(python_nbustags);
			solver_python_log(1,"init_busid(): bus id = <natural index>");
		}
		// Py_INCREF(pBusdata);
		for ( size_t n = 0 ; n < python_nbustags ; n++ )
		{
			solver_python_log(1,"init_busid(): bus tag '%s'", python_bustags[n]);
		}
	}
}

void init_branchdata(void)
{
	if ( python_branchtags == NULL )
	{
		init_branchtags();
		if ( python_branchid )
		{
			pBranchdata = PyDict_New();
			solver_python_log(1,"init_branchid(): branch id = %s", python_branchid);
		}
		else
		{
			pBranchdata = PyList_New(python_nbranchtags);
			solver_python_log(1,"init_branchid(): branch id = <natural index>");
		}
		// Py_INCREF(pBranchdata);
		for ( size_t n = 0 ; n < python_nbranchtags ; n++ )
		{
			solver_python_log(1,"init_branchtags(): branch tag '%s'", python_branchtags[n]);
		}
	}
}

void init_learntags(void)
{
	const char *tags = python_learndata;
	python_nlearntags = strlen(tags,',');
	python_learntags = new const char*[python_nlearntags];
	for ( size_t n = 0 ; n < python_nlearntags ; n++ )
	{
		python_learntags[n] = strdup(tags,',',&tags);
	}
}

void init_learndata(void)
{
	if ( python_learntags == NULL )
	{
		pLearndata = PyDict_New();
		// Py_INCREF(pLearndata);
		init_learntags();
	}
}

int solver_python_init(void)
{
	errno = 0;
	if ( solver_py_status == SPS_INIT )
	{
		solver_py_status = solver_python_config();
		const char *status_text[] = {"INIT","READY","FAILED","DISABLED","UNKNOWN"};
		if ( (int)solver_py_status >= 0 && (int)solver_py_status < (int)(sizeof(status_text)/sizeof(status_text[0])) )
		{
			solver_python_log(1,"solver_python_init(): solver_py_status = SPS_%s",status_text[solver_py_status]);
		}
		else
		{
			solver_python_log(1,"solver_python_init(): solver_py_status unknown");
		}

		if ( solver_py_status == SPS_READY && module_import_name != NULL )
		{
			pModule = python_import(module_import_name,module_import_path);
			if ( pModule == NULL )
			{
				solver_python_log(0,"ERROR: solver_python_init(): module '%s' not found in path '%s'", module_import_name, module_import_name?module_import_name:"");
			}
		}
	}
	if ( solver_py_status == SPS_READY )
	{
		if ( pModel == NULL )
		{
			pModel = PyDict_New();
			// Py_INCREF(pModel);
		}
		if ( pBusdata == NULL )
		{
			init_busdata();
		}
		if ( pBranchdata == NULL )
		{
			init_branchdata();
		}
		if ( pLearndata == NULL )
		{
			init_learndata();
		}
		return 0;
	}
	return errno ? errno : -1 ;
}

void set_bustags(PyObject *pModel)
{
	PyObject *data = PyDict_GetItemString(pModel,"bustags");
	if ( data == NULL )
	{
		data = PyDict_New();
		// Py_INCREF(data);
		for ( size_t n = 0 ; n < python_nbustags ; n++ )
		{
			PyObject *pLong = PyLong_FromSize_t(n);
			// Py_INCREF(pLong);
			PyDict_SetItemString(data,python_bustags[n],pLong);
		}
		PyDict_SetItemString(pModel,"bustags",data);
	}
}

void set_branchtags(PyObject *pModel)
{
	PyObject *data = PyDict_GetItemString(pModel,"branchtags");
	if ( data == NULL )
	{
		data = PyDict_New();
		// Py_INCREF(data);
		for ( size_t n = 0 ; n < python_nbranchtags ; n++ )
		{
			PyObject *pLong = PyLong_FromSize_t(n);
			// Py_INCREF(pLong);
			PyDict_SetItemString(data,python_branchtags[n],pLong);
		}
		PyDict_SetItemString(pModel,"branchtags",data);
	}
}

void complex_to_mag(double *x, void *z, bool inverse)
{
	if ( inverse )
	{
		((complex*)z)->Mag(*x);
	}
	else
	{
		*x = ((complex*)z)->Mag();
	}
}

void complex_to_arg(double *x, void *z, bool inverse)
{
	if ( inverse )
	{
		((complex*)z)->Arg(*x);
	}
	else
	{
		*x = ((complex*)z)->Arg();
	}
}

void int_to_double(double *x, void *c, bool inverse)
{
	if ( inverse )
	{
		*(int*)c = (int)(*x);
	}
	else
	{
		*x = (double)(*(int*)c);
	}
}

void uchar_to_double(double *x, void *c, bool inverse)
{
	if ( inverse )
	{
		*(unsigned char*)c = (unsigned char)(*x);
	}
	else
	{
		*x = (double)(*(unsigned char*)c);
	}
}

void ref_to_mag(double *x, void *c, bool inverse)
{
	complex *z = (complex*)c;
	if ( inverse )
	{
		z->Mag(*x);
	}
	else
	{
		*x = z->Mag();
	}
}

void ref_to_arg(double *x, void *c, bool inverse)
{
	complex *z = (complex*)c;
	if ( inverse )
	{
		z->Arg(*x);
	}
	else
	{
		*x = z->Arg();
	}
}

void ref_to_ang(double *x, void *c, bool inverse)
{
	complex *z = (complex*)c;
	if ( inverse )
	{
		z->Ang(*x);
	}
	else
	{
		*x = z->Ang();
	}
}

// bus/branch data mapping
static BUSDATA bus_t;
static BRANCHDATA branch_t;
#define DATA(S,T,X,D,C) 		{T, (int64)(&(S##_t.X))-(int64)(&S##_t),sizeof(S##_t),D,C}
#define DATA_R(S,T,X,R,D,C) 	{T, (int64)(&(S##_t.X))-(int64)(&S##_t),sizeof(S##_t),D,C,s_map::DOUBLE,(int64)&(S##_t.X R)-(int64)(S##_t.X)}
#define DATA_X(S,T,X,R,D,C) 	{T, (int64)(&(S##_t.X))-(int64)(&S##_t),sizeof(S##_t),D,C,s_map::PDOUBLE,(int64)&(S##_t.X R)-(int64)(S##_t.X)}
#define DATA_C(S,T,X,R,D,C) 	{T, (int64)(&(S##_t.X))-(int64)(&S##_t),sizeof(S##_t),D,C,s_map::PCOMPLEX,(int64)&(S##_t.X R)-(int64)(S##_t.X)}
#define THREEPHASE_R(B,X,D) \
	DATA_R(B,#X "A",X,[0],D,NULL), \
	DATA_R(B,#X "B",X,[1],D,NULL), \
	DATA_R(B,#X "C",X,[2],D,NULL)
#define THREEPHASE_C(B,X,D) \
	DATA_X(B,#X "Ar",X,[0].r,D,NULL), \
	DATA_X(B,#X "Ai",X,[0].i,D,NULL), \
	DATA_X(B,#X "Br",X,[1].r,D,NULL), \
	DATA_X(B,#X "Bi",X,[1].i,D,NULL), \
	DATA_X(B,#X "Cr",X,[2].r,D,NULL), \
	DATA_X(B,#X "Ci",X,[2].i,D,NULL)
#define THREEPHASE_M(B,X,D) \
	DATA_C(B,#X "Am",X,[0],D,ref_to_mag), \
	DATA_C(B,#X "Aa",X,[0],D,ref_to_arg), \
	DATA_C(B,#X "Ad",X,[0],D,ref_to_ang), \
	DATA_C(B,#X "Bm",X,[1],D,ref_to_mag), \
	DATA_C(B,#X "Ba",X,[1],D,ref_to_arg), \
	DATA_C(B,#X "Bd",X,[1],D,ref_to_ang), \
	DATA_C(B,#X "Cm",X,[2],D,ref_to_mag), \
	DATA_C(B,#X "Ca",X,[2],D,ref_to_arg), \
	DATA_C(B,#X "Cd",X,[2],D,ref_to_ang)
#define THREEPHASE_N(B,X,D) \
	DATA_X(B,#X "0",X,[0],D,NULL), \
	DATA_X(B,#X "1",X,[0],D,NULL), \
	DATA_X(B,#X "2",X,[1],D,NULL)
#define SYMMETRIC_C(B,X,D) \
	DATA_X(B,#X "AAr",X,[0].r,D,NULL), \
	DATA_X(B,#X "AAi",X,[0].i,D,NULL), \
	DATA_X(B,#X "ABr",X,[1].r,D,NULL), \
	DATA_X(B,#X "ABi",X,[1].i,D,NULL), \
	DATA_X(B,#X "ACr",X,[2].r,D,NULL), \
	DATA_X(B,#X "ACi",X,[2].i,D,NULL), \
	DATA_X(B,#X "BBr",X,[4].r,D,NULL), \
	DATA_X(B,#X "BBi",X,[4].i,D,NULL), \
	DATA_X(B,#X "BCr",X,[5].r,D,NULL), \
	DATA_X(B,#X "BCi",X,[5].i,D,NULL), \
	DATA_X(B,#X "CCr",X,[8].r,D,NULL), \
	DATA_X(B,#X "CCi",X,[8].i,D,NULL)

typedef unsigned int e_dir;
#define ED_INIT 0x01
#define ED_IN   0x02
#define ED_OUT  0x04

// TODO: for performance reasons this should be separate init/in/out arrays
// alternatively an init, in, and out index can be used to iterate efficiently
static struct s_map
{
	const char *tag;
	int64 offset;
	int64 size;
	e_dir dir;
	void (*convert)(double*,void*,bool);
	enum {
		DOUBLE   =0, // value is at offset
		PDOUBLE  =1, // pointer to value is at offset
		PCOMPLEX =2, // pointer to value is converted using a method
	} type;
	int64 ref_offset;
} busmap[] = 
{
	DATA(bus,"type",type,ED_INIT,int_to_double),
	DATA(bus,"phases",phases,ED_OUT,uchar_to_double),
	DATA(bus,"origphases",origphases,ED_INIT,uchar_to_double),
	DATA(bus,"mva_base",mva_base,ED_INIT,NULL),
	DATA(bus,"volt_base",volt_base,ED_INIT,NULL),
	THREEPHASE_C(bus,V,ED_INIT|ED_IN|ED_OUT),
	THREEPHASE_M(bus,V,ED_INIT|ED_IN|ED_OUT),
	THREEPHASE_C(bus,S,ED_OUT),
	THREEPHASE_C(bus,Y,ED_OUT),
	THREEPHASE_C(bus,I,ED_OUT),
	THREEPHASE_C(bus,prerot_I,ED_OUT),
	THREEPHASE_C(bus,S_dy,ED_OUT),
	THREEPHASE_C(bus,Y_dy,ED_OUT),
	THREEPHASE_C(bus,I_dy,ED_OUT),
	THREEPHASE_C(bus,PGenTotal,ED_OUT),
	THREEPHASE_C(bus,DynCurrent,ED_OUT),
	THREEPHASE_R(bus,PL,ED_OUT),
	THREEPHASE_R(bus,QL,ED_OUT),
	SYMMETRIC_C(bus,full_Y,ED_OUT),
	SYMMETRIC_C(bus,full_Y_all,ED_OUT),
	THREEPHASE_C(bus,full_Y_load,ED_OUT),
	THREEPHASE_N(bus,Jacob_A,ED_OUT),
	THREEPHASE_N(bus,Jacob_B,ED_OUT),
	THREEPHASE_N(bus,Jacob_C,ED_OUT),
	THREEPHASE_N(bus,Jacob_D,ED_OUT),
}, branchmap[] = 
{
	DATA(branch,"phases",phases,ED_OUT,uchar_to_double),
	DATA(branch,"origphases",origphases,ED_INIT,uchar_to_double),
	DATA(branch,"faultphases",faultphases,ED_OUT,uchar_to_double),
	DATA(branch,"from",from,ED_INIT,int_to_double),
	DATA(branch,"to",to,ED_INIT,int_to_double),
	DATA(branch,"lnk_type",lnk_type,ED_INIT,uchar_to_double),
	DATA(branch,"v_ratio",v_ratio,ED_INIT,NULL),
	DATA(branch,"fault_link_below",fault_link_below,ED_OUT,int_to_double),
	THREEPHASE_C(branch,Yfrom,ED_OUT),
	THREEPHASE_C(branch,Yto,ED_OUT),
	THREEPHASE_C(branch,YSfrom,ED_OUT),
	THREEPHASE_C(branch,YSto,ED_OUT),
};
static int *bus_index = NULL;
static int *branch_index = NULL;

void sync_double(PyObject *data, size_t n, void *ptr, void (*convert)(double*,void*,bool), bool inverse)
{
	PyObject *pValue = PyList_GetItem(data,n);
	if ( inverse )
	{
		if ( pValue && PyFloat_Check(pValue) )
		{
			double x = PyFloat_AsDouble(pValue);
			if ( convert )
			{
				convert(&x,ptr,true);
			}
			else
			{
				*(double*)ptr = x;
			}
		}
	}
	else
	{
		double x = convert ? (convert(&x,ptr,false),x) : *(double*)ptr;
		if ( pValue == NULL || ! PyFloat_Check(pValue) || PyFloat_AsDouble(pValue) != x )
		{
			if ( pValue != NULL )
			{
				Py_DECREF(pValue);
			}
			PyObject *pDouble = PyFloat_FromDouble(x);
			// Py_INCREF(pDouble);
			PyList_SetItem(data,n,pDouble);
		}
	}
}

void sync_double_ref(PyObject *data, size_t n, void *ptr, int64 offset, bool inverse)
{
	double **ppx = (double**)ptr;
	if ( ppx == NULL )
		return;
	double *px = (double*)(((char*)(*ppx))+offset);
	if ( px == NULL )
		return;
	double &x = *px;
	PyObject *pValue = PyList_GetItem(data,n);
	if ( inverse )
	{
		if ( pValue && PyFloat_Check(pValue) )
		{
			x = PyFloat_AsDouble(pValue);
		}
	}
	else
	{
		if ( pValue == NULL || ! PyFloat_Check(pValue) || PyFloat_AsDouble(pValue) != x )
		{
			if ( pValue != NULL )
			{
				Py_DECREF(pValue);
			}
			PyObject *pDouble = PyFloat_FromDouble(x);
			// Py_INCREF(pDouble);
			PyList_SetItem(data,n,pDouble);
		}
	}
}

void sync_complex_ref(PyObject *data, size_t n, void *ptr, void (*convert)(double*,void*,bool), int64 offset, bool inverse)
{
	complex **ppz = (complex**)ptr;
	if ( ppz == NULL )
		return;
	complex *pz = (complex*)(((char*)(*ppz))+offset);
	if ( pz == NULL )
		return;
	PyObject *pValue = PyList_GetItem(data,n);
	if ( inverse )
	{
		if ( pValue && PyFloat_Check(pValue) )
		{
			double x = PyFloat_AsDouble(pValue);
			convert(&x,pz,true);
		}
	}
	else
	{
		double x;
		convert(&x,pz,false);
		if ( pValue == NULL || ! PyFloat_Check(pValue) || PyFloat_AsDouble(pValue) != x )
		{
			if ( pValue != NULL )
			{
				Py_DECREF(pValue);
			}
			PyObject *pDouble = PyFloat_FromDouble(x);
			// Py_INCREF(pDouble);
			PyList_SetItem(data,n,pDouble);
		}
	}
}

void sync_none(PyObject *data, size_t n, bool inverse)
{
	if ( ! inverse )
	{
		PyObject *pValue = PyList_GetItem(data,n);
		if ( pValue != Py_None )
		{
			if ( pValue != NULL )
			{
				Py_DECREF(pValue);
			}
			PyList_SetItem(data,n,Py_None);
			// Py_INCREF(Py_None);
		}
	}
}

void sync_data(PyObject *data, size_t n, void *source, struct s_map *map, e_dir dir)
{
	if ( dir & map->dir )
	{	
		if ( map->offset >= 0 && map->offset < map->size )
		{
			void *ptr = (void*)((char*)source + map->offset);
			if ( ptr == NULL )
			{
				return;
			}
			switch ( map->type )
			{
			case s_map::DOUBLE:
				sync_double(data,n,ptr,map->convert,(dir&ED_IN));
				break;
			case s_map::PDOUBLE:
				sync_double_ref(data,n,ptr,map->ref_offset,(dir&ED_IN));
				break;
			case s_map::PCOMPLEX:
				sync_complex_ref(data,n,ptr,map->convert,map->ref_offset,(dir&ED_IN));
				break;
			default:
				sync_none(data,n,(dir&ED_IN));
				break;
			}
		}
		else
		{
			sync_none(data,n,(dir&ED_IN));
		}
	}
}

void sync_busdata(PyObject *pModel,unsigned int &bus_count,BUSDATA *&bus,e_dir dir)
{
	PyObject *busdata = PyDict_GetItemString(pModel,"busdata");
	if ( busdata == NULL )
	{
		busdata = PyList_New(python_nbustags);
		// Py_INCREF(busdata);
		PyDict_SetItemString(pModel,"busdata",busdata);
		bus_index = new int[python_nbustags];
		memset(bus_index,-1,python_nbustags*sizeof(int));
		for ( size_t m = 0 ; m < sizeof(busmap)/sizeof(busmap[0]) ; m++ )
		{
			// printf("busmap %d: {tag:'%s',offset:%lld,size:%lld,dir:%u,is_ref:%d,ref_offset:%lld}\n",
			// 	(int)m,busmap[m].tag,busmap[m].offset,busmap[m].size,busmap[m].dir,busmap[m].is_ref,busmap[m].ref_offset);
			int t = strfind(python_nbustags,python_bustags,busmap[m].tag);
			PyObject *data = PyList_New(bus_count);
			// Py_INCREF(data);
			PyList_SetItem(busdata,t,data);
			if ( t >= 0 )
			{
				bus_index[t] = m;
				for ( size_t n = 0 ; n < bus_count ; n++ )
				{
					sync_data(data,n,(void*)&bus[n],&busmap[m],ED_INIT);
				}
			}
		}
		for ( size_t t = 0 ; t < python_nbustags ; t++ )
		{
			PyObject *data = PyList_GetItem(busdata,t);
			// Py_INCREF(data);
			if ( data == NULL )
			{
				PyList_SetItem(busdata,t,Py_None);
				Py_INCREF(Py_None);
			}
		}
	}
	for ( size_t t = 0 ; t < python_nbustags ; t++ )
	{
		int m = bus_index[t];
		PyObject *data = PyList_GetItem(busdata,t);
		if ( m >= 0 )
		{
			for ( size_t n = 0 ; n < bus_count ; n++ )
			{
				sync_data(data,n,(void*)&bus[n],&busmap[m],dir);
			}
		}
		else if ( m == -1 )
		{
			if ( dir & (ED_INIT|ED_OUT) )
			{
				for ( size_t n = 0 ; n < bus_count ; n++ )
				{
					PyList_SetItem(data,n,Py_None);
					Py_INCREF(Py_None);
				}
			}
			bus_index[t] = -2;
			gl_error("%s: bus tag '%s' not found", (const char*)solver_py_config, python_bustags[t]);
		}
	}
}

void sync_branchdata(PyObject *pModel,unsigned int &branch_count,BRANCHDATA *&branch,e_dir dir)
{
	PyObject *branchdata = PyDict_GetItemString(pModel,"branchdata");
	if ( branchdata == NULL )
	{
		branchdata = PyList_New(python_nbranchtags);
		PyDict_SetItemString(pModel,"branchdata",branchdata);
		Py_INCREF(branchdata);
		branch_index = new int[python_nbranchtags];
		memset(branch_index,-1,python_nbranchtags*sizeof(int));
		for ( size_t m = 0 ; m < sizeof(branchmap)/sizeof(branchmap[0]) ; m++ )
		{
			int t = strfind(python_nbranchtags,python_branchtags,branchmap[m].tag);
			if ( t >= 0 )
			{
				PyObject *data = PyList_New(branch_count);
				// Py_INCREF(data);
				PyList_SetItem(branchdata,t,data);
				branch_index[t] = m;
				for ( size_t n = 0 ; n < branch_count ; n++ )
				{
					sync_data(data,n,(void*)&branch[n],&branchmap[m],ED_INIT);
				}
			}
		}
		for ( size_t t = 0 ; t < python_nbranchtags ; t++ )
		{
			PyObject *data = PyList_GetItem(branchdata,t);
			if ( data == NULL )
			{
				PyList_SetItem(branchdata,t,Py_None);
				Py_INCREF(Py_None);
			}
		}
	}
	for ( size_t t = 0 ; t < python_nbranchtags ; t++ )
	{
		int m = branch_index[t];
		PyObject *data = PyList_GetItem(branchdata,t);
		if ( m >= 0 )
		{
			for ( size_t n = 0 ; n < branch_count ; n++ )
			{
				sync_data(data,n,(void*)&branch[n],&branchmap[m],dir);
			}
		}
		else if ( m == -1 )
		{
			if ( dir & (ED_INIT|ED_OUT) )
			{
				for ( size_t n = 0 ; n < branch_count ; n++ )
				{
					PyList_SetItem(data,n,Py_None);
					Py_INCREF(Py_None);
				}
			}
			branch_index[t] = -2;
			gl_error("%s: branch tag '%s' not found", (const char*)solver_py_config, python_branchtags[t]);
		}
	}
}

static PyObject *sync_model(
	unsigned int &bus_count,
	BUSDATA *&bus,
	unsigned int &branch_count,
	BRANCHDATA *&branch,
	e_dir dir)
{
	set_bustags(pModel);
	set_branchtags(pModel);
	sync_busdata(pModel,bus_count,bus,dir);
	sync_branchdata(pModel,branch_count,branch,dir);
	if ( pKwargs )
	{
		PyDict_SetItemString(pModel,"options",pKwargs);
	}
	return pModel;
}

int solver_python_solve (
	unsigned int &bus_count,
	BUSDATA *&bus,
	unsigned int &branch_count,
	BRANCHDATA *&branch,
	NR_SOLVER_STRUCT *powerflow_values, 
	NRSOLVERMODE powerflow_type , 
	NR_MESHFAULT_IMPEDANCE *mesh_imped_values, 
	bool *bad_computations,
	int64 &iterations)
{
	SolverTimer timer("solve");
	int result = 0;
	if ( pModule )
	{
		sync_model(bus_count,bus,branch_count,branch,ED_OUT);
		PyObject *pResult = NULL;
		if ( ! python_call(pModule,(void*)&pResult,"solve","O",pModel) )
		{
			solver_python_log(1,"solver_python_solve(bus_count=%d,...): solver failed",bus_count);
			result = -1001;
		}
		else if ( pResult && PyLong_Check(pResult) )
		{
			result = PyLong_AsLong(pResult);
			if ( result >= 0 )
			{
				sync_model(bus_count,bus,branch_count,branch,ED_IN);
			}
		}
		else
		{
			solver_python_log(0,"ERROR: solver_python_solve(bus_count=%d,...): result is not a valid long value",bus_count);
			result = -1002;
		}
		if ( pResult )
		{
			Py_DECREF(pResult);
		}
	}
	else
	{
		solver_python_log(0,"ERROR: solver_python_solve(bus_count=%d,...): gridlabd module not yet ready",bus_count);
		result = -1005;
	}
	return result;
}

PyObject *check_dict(PyObject *pObj,const char *name)
{
	PyObject *pDict = PyDict_GetItemString(pObj,name);
	if ( pDict == NULL )
	{
		pDict = PyDict_New();
		// Py_INCREF(pDict);
		PyDict_SetItemString(pObj,name,pDict); 
	}
	return pDict;
}
void sync_powerflow_values(PyObject *pSolution, size_t buscount, NR_SOLVER_STRUCT *powerflow_values)
{
	if ( strfind(python_nlearntags,python_learntags,"powerflow_values") >= 0
		|| strfind(python_nlearntags,python_learntags,"powerflow_values.deltaI_NR") >= 0 )
	{
		PyObject *pDict = check_dict(pSolution,"powerflow_values");
		if ( powerflow_values->deltaI_NR )
		{
			PyObject *pData = PyList_New(powerflow_values->total_variables*2);
			PyDict_SetItemString(pDict,"deltaI_NR",pData);
			// Py_INCREF(pData);
			for ( size_t n = 0 ; n < powerflow_values->total_variables*2 ; n++ )
			{
				PyList_SetItem(pData,n,PyFloat_FromDouble(powerflow_values->deltaI_NR[n]));
			}
		}
		else
		{
			PyDict_SetItemString(pDict,"deltaI_NR",Py_None);
			Py_INCREF(Py_None);
		}
	}

	if ( strfind(python_nlearntags,python_learntags,"powerflow_values") >= 0
		|| strfind(python_nlearntags,python_learntags,"powerflow_values.BA_diag") >= 0 )
	{
		PyObject *pDict = check_dict(pSolution,"powerflow_values");
		if ( powerflow_values->BA_diag )
		{
			PyObject *pData = PyList_New(buscount);
			// Py_INCREF(pData);
			PyDict_SetItemString(pDict,"BA_diag",pData);
			for ( size_t n = 0 ; n < buscount ; n++ )
			{
				Bus_admit &bus = powerflow_values->BA_diag[n];
				PyObject *pBus = PyList_New(bus.size);
				for ( size_t r = 0 ; r < (size_t)bus.size ; r++ )
				{
					PyObject *pRow = PyList_New(bus.size);
					PyList_SetItem(pBus,r,pRow);
					for ( size_t c = 0 ; c < (size_t)bus.size ; c++ )
					{
						PyObject *pValue = Py_BuildValue("(dd)",bus.Y[r][c].r,bus.Y[r][c].i);
						// Py_INCREF(pValue);
						PyList_SetItem(pRow,c,pValue);
					}
				}
				PyObject *pValue = Py_BuildValue("(iO)",bus.row_ind,pBus);
				PyList_SetItem(pData,n,pValue);
			}
		}
		else
		{
			PyDict_SetItemString(pDict,"BA_diag",Py_None);
			Py_INCREF(Py_None);
		}
	}

	if ( strfind(python_nlearntags,python_learntags,"powerflow_values") >= 0
		|| strfind(python_nlearntags,python_learntags,"powerflow_values.Y_offdiag_PQ") >= 0 )
	{
		PyObject *pDict = check_dict(pSolution,"powerflow_values");
		if ( powerflow_values->Y_offdiag_PQ )
		{
			PyObject *pData = PyList_New(powerflow_values->size_offdiag_PQ*2);
			// Py_INCREF(pData);
			PyDict_SetItemString(pDict,"Y_offdiag_PQ",pData);
			for ( size_t n = 0 ; n < powerflow_values->size_offdiag_PQ*2 ; n++ )
			{
				PyObject *pValue = Py_BuildValue("(iid)",powerflow_values->Y_offdiag_PQ[n].row_ind,powerflow_values->Y_offdiag_PQ[n].col_ind,powerflow_values->Y_offdiag_PQ[n].Y_value);
				PyList_SetItem(pData,n,pValue);
			}
		}
		else
		{
			PyDict_SetItemString(pDict,"Y_offdiag_PQ",Py_None);
			Py_INCREF(Py_None);
		}
	}

	if ( strfind(python_nlearntags,python_learntags,"powerflow_values") >= 0
		|| strfind(python_nlearntags,python_learntags,"powerflow_values.Y_diag_fixed") >= 0 )
	{
		PyObject *pDict = check_dict(pSolution,"powerflow_values");
		if ( powerflow_values->Y_diag_fixed )
		{
			PyObject *pData = PyList_New(powerflow_values->size_diag_fixed*2);
			// Py_INCREF(pData);
			PyDict_SetItemString(pDict,"Y_diag_fixed",pData);
			for ( size_t n = 0 ; n < powerflow_values->size_diag_fixed*2 ; n++ )
			{
				PyObject *pValue = Py_BuildValue("(iid)",powerflow_values->Y_diag_fixed[n].row_ind,powerflow_values->Y_diag_fixed[n].col_ind,powerflow_values->Y_diag_fixed[n].Y_value);
				PyList_SetItem(pData,n,pValue);
			}
		}
		else
		{
			PyDict_SetItemString(pDict,"Y_diag_fixed",Py_None);
			Py_INCREF(Py_None);
		}
	}

	if ( strfind(python_nlearntags,python_learntags,"powerflow_values") >= 0
		|| strfind(python_nlearntags,python_learntags,"powerflow_values.Y_Amatrix") >= 0 )
	{
		PyObject *pDict = check_dict(pSolution,"powerflow_values");
		if ( powerflow_values->Y_Amatrix )
		{
			PyObject *pData = PyDict_New();
			// Py_INCREF(pData);
			PyDict_SetItemString(pDict,"Y_Amatrix",pData);
			PyObject *pHeap = PyList_New(0);
			// Py_INCREF(pHeap);
			PyDict_SetItemString(pData,"llheap",pHeap);
			for ( SP_E *p = powerflow_values->Y_Amatrix->llheap ; p != NULL ; p = p->next )
			{
				PyObject *pValue = Py_BuildValue("(id)",p->row_ind,p->value);
				PyList_Append(pHeap,pValue);
			}
			PyObject *pCols = PyList_New(0);
			// Py_INCREF(pCols);
			PyDict_SetItemString(pData,"cols",pCols);
			for ( size_t n = 0 ; n < powerflow_values->Y_Amatrix->ncols ; n++ )
			{ 
				if ( powerflow_values->Y_Amatrix->cols[n] == NULL )
				{
					continue;
				}
				PyObject *pCol = PyList_New(0);
				// Py_INCREF(pCol);
				PyList_Append(pCols,pCol);
				for ( SP_E *p = powerflow_values->Y_Amatrix->cols[n] ; p != NULL ; p = p->next )
				{
					PyObject *pValue = Py_BuildValue("(id)",p->row_ind,p->value);
					PyList_Append(pCol,pValue);
				}
			}
		}
		else
		{
			PyDict_SetItemString(pDict,"Y_Amatrix",Py_None);
			Py_INCREF(Py_None);
		}
	}
}

void sync_powerflow_type(PyObject *pSolution, NRSOLVERMODE powerflow_type)
{
	PyObject *pLong = PyLong_FromLong((long)powerflow_type);
	// Py_INCREF(pLong);
	PyDict_SetItemString(pSolution,"powerflow_type",pLong);
}

void sync_mesh_imped_values(PyObject *pSolution, NR_MESHFAULT_IMPEDANCE *mesh_imped_values)
{
	if ( strfind(python_nlearntags,python_learntags,"mesh_imped_values") >= 0 )
	{
		// TODO external solvers are not supported yet
		static bool done = false;
		if ( ! done )
		{
			solver_python_log(0,"WARNING: %s: learndata mesh_imped_values is not supported by this solver", (const char*)solver_py_config);
			done = true;
		}
		PyDict_SetItemString(pSolution,"mesh_imped_values",Py_None);
		Py_INCREF(Py_None);
	}
}

void sync_bad_computations(PyObject *pSolution, bool *bad_computations)
{
	if ( bad_computations )
	{
		if ( *bad_computations )
		{
			PyDict_SetItemString(pSolution,"bad_computations",Py_True);
			Py_INCREF(Py_True);
		}
		else
		{
			PyDict_SetItemString(pSolution,"bad_computations",Py_False);
			Py_INCREF(Py_False);
		}
	}
	else
	{
		PyDict_SetItemString(pSolution,"bad_computations",Py_None);
		Py_INCREF(Py_None);
	}
}

void sync_iterations(PyObject *pSolution,int64 iterations)
{
	PyObject *pLong = PyLong_FromLong(iterations);
	// Py_INCREF(pLong);
	PyDict_SetItemString(pSolution,"iterations",pLong);
}

PyObject *sync_solution(
	size_t buscount,
	NR_SOLVER_STRUCT *powerflow_values,
	NRSOLVERMODE powerflow_type,
	NR_MESHFAULT_IMPEDANCE *mesh_imped_values,
	bool *bad_computations,
	int64 iterations)
{
	sync_bad_computations(pSolution,bad_computations);
	sync_iterations(pSolution,iterations);
	sync_powerflow_values(pSolution,buscount,powerflow_values);
	sync_powerflow_type(pSolution,powerflow_type);
	sync_mesh_imped_values(pSolution,mesh_imped_values);
	return pSolution;
}


void solver_python_learn (
	unsigned int bus_count,
	BUSDATA *bus, 
	unsigned int branch_count, 
	BRANCHDATA *branch, 
	NR_SOLVER_STRUCT *powerflow_values, 
	NRSOLVERMODE powerflow_type , 
	NR_MESHFAULT_IMPEDANCE *mesh_imped_values, 
	bool *bad_computations,
	int64 iterations)
{
	SolverTimer timer("learn");
	if ( pModule )
	{
		sync_model(bus_count,bus,branch_count,branch,ED_OUT);
		if ( pSolution == NULL )
		{
			pSolution = PyDict_Copy(pModel);
		}
		sync_solution(bus_count,powerflow_values,powerflow_type,mesh_imped_values,bad_computations,iterations);
		if ( ! python_call(pModule,NULL,"learn","O",pSolution) )
		{
			solver_python_log(1,"solver_python_solve(bus_count=%d,...): learn failed",bus_count);
		}
	}
	if ( model_busdump || model_branchdump )
	{
		solver_dump(bus_count,bus,branch_count,branch,false);
	}
}

void solver_dump(unsigned int &bus_count,
				 BUSDATA *&bus,
				 unsigned int &branch_count,
				 BRANCHDATA *&branch,
				 bool allow_generic_dumpfile)
{
	FILE *fh;
	const char *bustype[] = {"PQ","PV","SWING","SWING_PG"};
	const char *busflag[] = {"NONE","HASSOURCE","ISSOURCE","HASSOURCE|ISSOURCE"};
	char phases[256][9];
	for ( int c = 0 ; c < 256 ; c++ )
	{
		memset(phases[c],0,sizeof(phases[c]));
		if ( c&0x80 ) strcat(phases[c],"S"); // split
		if ( c&0x20 ) strcat(phases[c],"X"); // transformer
		if ( c&0x04 ) strcat(phases[c],"A"); // phase A
		if ( c&0x02 ) strcat(phases[c],"B"); // phase B
		if ( c&0x01 ) strcat(phases[c],"C"); // phase C
		if ( c&0x08 ) strcat(phases[c],"D"); // delta
		if ( c&0x10 ) strcat(phases[c],"Z"); // different children	
		if ( c&0x40 ) strcat(phases[c],"H"); // house connected node
	}
	//
	// Bus dump
	//
	if ( model_busdump == NULL && allow_generic_dumpfile )
		model_busdump = "busdump.csv";
	gl_verbose("dumping bus data to '%s'",model_busdump);
	fh = fopen(model_busdump,"w");
	if ( fh == NULL )
	{
		solver_python_log(0,"ERROR solver_dump(): unable to open bus dumpfile '%s' for write", model_busdump);
		return;
	}
	fprintf(fh,"id,name,type,phases,origphases,busflags,"
		"vbase,mvabase,"
#define POLAR(X) #X "Am," #X "Aa," #X "Bm," #X "Ba," #X "Cm," #X "Ca,"
#define RECT(X)  #X "Ar," #X "Ai," #X "Br," #X "Bi," #X "Cr," #X "Ci,"
#define DELIM ""
#include "solver_ml_branchdump.h"
#undef POLAR
#undef RECT
#undef DELIM
		"PA,QA,PB,QB,PC,QC,"
		"YcAAr,YcAAi,YcABr,YcABi,YcACr,YcACi,YcBBr,YcBBi,YcBCr,YcBCu,YcCCr,YcCCi,"
		"YsAAr,YsAAi,YsABr,YsABi,YsACr,YsACi,YsBBr,YsBBi,YsBCr,YsBCi,YsCCr,YsCCi,"
		"YlAr,YlAi,YlBr,YlBi,YlCr,YlCi,"
		"JA0,JA1,JA2,"
		"JB0,JB1,JB2,"
		"JC0,JC1,JC2,"
		"JD0,JD1,JD2,"
		"EOL\n");
	for ( unsigned int m = 0 ; m < bus_count ; m++ )
	{
		BUSDATA *b = bus + m;
		char bustypeerr[32] = "ERROR", phaseserr[32] = "ERROR", origphaseserr[32] = "ERROR", busflagerr[32] = "ERROR";
		if ( bustype[b->type] == NULL ) snprintf(bustypeerr,sizeof(bustypeerr)-1,"ERROR:%d",b->type);
		if ( phases[b->phases] == NULL ) snprintf(phaseserr,sizeof(phaseserr)-1,"ERROR:%d",(int)b->phases);
		if ( phases[b->origphases] == NULL ) snprintf(origphaseserr,sizeof(origphaseserr)-1,"ERROR:%d",(int)b->origphases);
		if ( busflag[*(b->busflag)] == NULL ) snprintf(busflagerr,sizeof(busflagerr)-1,"ERROR:%d",(int)*(b->busflag));
		fprintf(fh,"%d,%s,%s,%s,%s,%s,"
			// Vbase and MVAbase
			"%g,%g,"
#define POLAR(X) "%g,%g,%g,%g,%g,%g," 
#define RECT(X) "%g,%g,%g,%g,%g,%g," 
#define DELIM ""
#include "solver_ml_branchdump.h"
#undef POLAR
#undef RECT
#undef DELIM
			// loads
			"%g,%g,%g,%g,%g,%g,"
			// Y matrices
			"%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,"
			"%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,"
			"%g,%g,%g,%g,%g,%g,"
			// Jacobians
			"%g,%g,%g,"
			"%g,%g,%g,"
			"%g,%g,%g,"
			"%g,%g,%g,"
			"%s\n",
			m,b->name?b->name:"",bustype[b->type]?bustype[b->type]:bustypeerr,phases[b->phases]?phases[b->phases]:phaseserr,phases[b->origphases]?phases[b->origphases]:origphaseserr,*(b->busflag)<sizeof(busflag)/sizeof(busflag[0])?busflag[*(b->busflag)]:busflagerr,
			b->volt_base > 0 ? b->volt_base : QNAN, b->mva_base > 0 ? b->mva_base : QNAN,
#define POLAR(X) (b->X)==NULL?QNAN:(b->X)[0].Mag(),(b->X)==NULL?QNAN:(b->X)[0].Ang(),(b->X)==NULL?QNAN:(b->X)[1].Mag(),(b->X)==NULL?QNAN:(b->X)[1].Ang(),(b->X)==NULL?QNAN:(b->X)[2].Mag(),(b->X)==NULL?QNAN:(b->X)[2].Ang()
#define RECT(X) (b->X)==NULL?QNAN:(b->X)[0].Re(),(b->X)==NULL?QNAN:(b->X)[0].Im(),(b->X)==NULL?QNAN:(b->X)[1].Re(),(b->X)==NULL?QNAN:(b->X)[1].Im(),(b->X)==NULL?QNAN:(b->X)[2].Re(),(b->X)==NULL?QNAN:(b->X)[2].Im()
#define DELIM ,
#include "solver_ml_branchdump.h"
#undef POLAR
#undef RECT
#undef DELIM
#define RECT(X,N) (b->X!=NULL)?(b->X[N]).Re():QNAN,(b->X!=NULL)?(b->X[N]).Im():QNAN
			b->PL[0],b->QL[0],b->PL[1],b->QL[1],b->PL[2],b->QL[2],
			RECT(full_Y,0),RECT(full_Y,1),RECT(full_Y,2),RECT(full_Y,4),RECT(full_Y,5),RECT(full_Y,8),
			RECT(full_Y_all,0),RECT(full_Y_all,1),RECT(full_Y_all,2),RECT(full_Y_all,4),RECT(full_Y_all,5),RECT(full_Y_all,8),
			RECT(full_Y_load,0),RECT(full_Y_load,1),RECT(full_Y_load,2),
			b->Jacob_A[0],b->Jacob_A[1],b->Jacob_A[2],
			b->Jacob_B[0],b->Jacob_B[1],b->Jacob_B[2],
			b->Jacob_C[0],b->Jacob_C[1],b->Jacob_C[2],
			b->Jacob_D[0],b->Jacob_D[1],b->Jacob_D[2],
			"EOL");
	}
	fclose(fh);

	//
	// Branch dump
	//
	if ( model_branchdump == NULL && allow_generic_dumpfile )
		model_branchdump = "branchdump.csv";
	gl_verbose("dumping branch data to '%s'",model_branchdump);
	fh = fopen(model_branchdump,"w");
	if ( fh == NULL )
	{
		solver_python_log(0,"ERROR solver_dump(): unable to open branch dumpfile '%s' for write", model_branchdump);
		return;
	}
	fprintf(fh,"id,name,type,phases,origphases,faultphases,from,to,fault_link_below,v_ratio,vratio,"
		"YfromAr,YfromAi,YfromBr,YfromBi,YfromCr,YfromCi,"
		"YtoAr,YtoAi,YtoBr,YtoBi,YtoCr,YtoCi,"
		"YSfromAr,YSfromAi,YSfromBr,YSfromBi,YSfromCr,YSfromCi,"
		"YStoAr,YStoAi,YStoBr,YStoBi,YStoCr,YStoCi,"
		"EOL\n");
	for ( unsigned int n = 0 ; n < branch_count ; n++ )
	{
		BRANCHDATA *b = branch+n;
		char phaseserr[32] = "ERROR", origphaseserr[32] = "ERROR", faultphaseserr[32] = "ERROR";
		const char *branchtypes[] = {"LINE","TRIPLEX","SWITCH","FUSE","TRANSFORMER","SECTIONALIZER","RECLOSER"};
		const char *branchtype = ( b->lnk_type >= 0 && b->lnk_type < sizeof(branchtypes)/sizeof(branchtypes[0]) ? branchtypes[b->lnk_type] : "ERROR" );
		if ( phases[b->phases] == NULL ) snprintf(phaseserr,sizeof(phaseserr)-1,"ERROR:%d",(int)b->phases);
		if ( phases[b->origphases] == NULL ) snprintf(origphaseserr,sizeof(origphaseserr)-1,"ERROR:%d",(int)b->origphases);
		if ( phases[b->faultphases] == NULL ) snprintf(faultphaseserr,sizeof(faultphaseserr)-1,"ERROR:%d",(int)b->faultphases);
		fprintf(fh,"%d,%s,%s,%s,%s,%s,%d,%d,%s,%g,%g,"
			"%g,%g,%g,%g,%g,%g,"
			"%g,%g,%g,%g,%g,%g,"
			"%g,%g,%g,%g,%g,%g,"
			"%g,%g,%g,%g,%g,%g,"
			"%s\n",
			n,b->name?b->name:"",branchtype,phases[b->phases]?phases[b->phases]:phaseserr,phases[b->origphases]?phases[b->origphases]:origphaseserr,phases[b->faultphases]?phases[b->faultphases]:faultphaseserr,b->from,b->to,b->fault_link_below?"FALSE":"TRUE",b->v_ratio,b->v_ratio,
			b->Yfrom[0].Re(),b->Yfrom[0].Im(),b->Yfrom[1].Re(),b->Yfrom[1].Im(),b->Yfrom[2].Re(),b->Yfrom[2].Im(),
			b->Yto[0].Re(),b->Yto[0].Im(),b->Yto[1].Re(),b->Yto[1].Im(),b->Yto[2].Re(),b->Yto[2].Im(),
			b->YSfrom[0].Re(),b->YSfrom[0].Im(),b->YSfrom[1].Re(),b->YSfrom[1].Im(),b->YSfrom[2].Re(),b->YSfrom[2].Im(),
			b->YSto[0].Re(),b->YSto[0].Im(),b->YSto[1].Re(),b->YSto[1].Im(),b->YSto[2].Re(),b->YSto[2].Im(),
			"EOL");
	}
	fclose(fh);
	if ( model_dump_handler )
	{
		if ( strncmp(model_dump_handler,"python:",7) == 0 )
		{
			if ( ! python_call(pModule,NULL,model_dump_handler+7,NULL) )
			{
				solver_python_log(0,"ERROR solver_dump(): model_dump_handler failed, rc = FALSE");
			}
		}
		else
		{
			int rc = system(model_dump_handler);
			if ( rc != 0 )
			{
				solver_python_log(0,"ERROR solver_dump(): model_dump_handler failed, rc = %d", rc);
			}
		}
	}
}
