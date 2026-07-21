/*
 *
 * yixue.ge add for oppo project
 *
 *
 */
#ifndef _OPPO_PROJECT_H_
#define _OPPO_PROJECT_H_

#include <linux/errno.h>

enum{
        HW_VERSION__UNKNOWN,
        HW_VERSION__10,   /*1452mV*/
        HW_VERSION__11,   /*1636 mV*/
        HW_VERSION__12,   /*1224 mV*/
        HW_VERSION__13,   /*900 mV*/
        HW_VERSION__14,   /*720 mV*/
        HW_VERSION__15,
        HW_VERSION__16,
};


enum{
        RF_VERSION__UNKNOWN,
        RF_VERSION__11,
        RF_VERSION__12,
        RF_VERSION__13,
        RF_VERSION__14,
        RF_VERSION__15,
        RF_VERSION__16,
        RF_VERSION__17,
        RF_VERSION__18,
        RF_VERSION__19,
        RF_VERSION__1A,
        RF_VERSION__1B,
        RF_VERSION__1C,
        RF_VERSION__1D,
        RF_VERSION__1E,
        RF_VERSION__1F,
};


#define GET_PCB_VERSION() (get_PCB_Version())

#define GET_MODEM_VERSION() (get_Modem_Version())
#define GET_OPERATOR_VERSION() (get_Operator_Version())



enum OPPO_PROJECT {
        OPPO_UNKOWN = 0,
        OPPO_16051 = 16051,
        OPPO_16103 = 16103,
        OPPO_16118 = 16118,
        OPPO_17011 = 17011,
        OPPO_17021 = 17021,
};

enum OPPO_OPERATOR {
        OPERATOR_UNKOWN                     = 0,
        OPERATOR_OPEN_MARKET          = 1,
        OPERATOR_CHINA_MOBILE             = 2,
        OPERATOR_CHINA_UNICOM              = 3,
        OPERATOR_CHINA_TELECOM                = 4,
        OPERATOR_FOREIGN                             = 5,
/*TongJing.Shi@EXP.DataComm.Phone, 2014.04.18, Add for exp just 3G no 4G*/
        OPERATOR_FOREIGN_WCDMA         = 6,   /*qifeng.liu 2014.08.07 FOR MAC*/
        OPERATOR_FOREIGN_RESERVED   = 7,    /*shaoming 2014/10/04 add for 14085's dual sim version*/
        OPERATOR_ALL_CHINA_CARRIER    = 8,   /*instead of TELECOM CARRIER because of history Tong.han@Bsp.Group.Tp add for all china carrier phone, 2015/03/23*/
        OPERATOR_ALL_CHINA_CARRIER_MOBILE = 9,    /*rendong.shi@Bsp.Group.Tp add for all china carrier MOBILE phone, 2016/01/07*/
        OPERATOR_ALL_CHINA_CARRIER_UNICOM = 10,    /*rendong.shi@Bsp.Group.Tp add for all china carrier UNICOM  phone, 2016/01/07*/
};

typedef enum OPPO_PROJECT OPPO_PROJECT;
#define OCPCOUNTMAX 4
typedef struct
{
        unsigned int                  nproject;
        unsigned char                 nmodem;
        unsigned char                 noperator;
        unsigned char                 npcbversion;
        unsigned char                 noppobootmode;
        unsigned char                 npmicocp[OCPCOUNTMAX];
} ProjectInfoCDTType;

#ifdef CONFIG_OPPO_COMMON_SOFT
void init_project_version(void);
int oppo_project_info_init(void);
unsigned int get_project(void);
unsigned int is_project(OPPO_PROJECT project);
unsigned char get_PCB_Version(void);
unsigned char get_Modem_Version(void);
unsigned char get_Operator_Version(void);
unsigned char get_Oppo_Boot_Mode(void);
int rpmb_is_enable(void);
#else
static inline void init_project_version(void)
{
}

static inline int oppo_project_info_init(void)
{
	return -ENODEV;
}

static inline unsigned int get_project(void)
{
	return 0;
}

static inline unsigned int is_project(OPPO_PROJECT project)
{
	return 0;
}

static inline unsigned char get_PCB_Version(void)
{
	return 0;
}

static inline unsigned char get_Modem_Version(void)
{
	return 0;
}

static inline unsigned char get_Operator_Version(void)
{
	return 0;
}

static inline unsigned char get_Oppo_Boot_Mode(void)
{
	return 0;
}

static inline int rpmb_is_enable(void)
{
	return 0;
}
#endif
#endif  /*_OPPO_PROJECT_H_*/
