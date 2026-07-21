// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/soc/qcom/smem.h>
#include <soc/oppo/oppo_project.h>

/* SMEM_ID_VENDOR1 */
#define SMEM_PROJECT			135

#define OEM_SEC_BOOT_REG			0x780350  /* sdm660 */
#define OEM_SEC_ENABLE_ANTIROLLBACK_REG		0x78019c /* sdm660 */
#define QFPROM_RAW_SERIAL_NUM			0x00786134
#define RPMB_KEY_PROVISIONED			0x00780178

static struct proc_dir_entry *oppoVersion;
static ProjectInfoCDTType *format;

enum {
	PRJ_VERSION = 1,
	PRJ_PCB_VERSION,
	PRJ_MODEM_TYPE,
	PRJ_OPERATOR_NAME,
	PRJ_OPPO_BOOTMODE,
	PRJ_SECURE_TYPE,
	PRJ_SECURE_STAGE,
	PRJ_SERIAL_ID,
	PRJ_OCP,
};

int oppo_project_info_init(void)
{
	size_t smem_size;
	void *smem_addr;
	int ret;

	if (READ_ONCE(format))
		return 0;

	smem_addr = qcom_smem_get(QCOM_SMEM_HOST_ANY, SMEM_PROJECT, &smem_size);
	if (IS_ERR(smem_addr)) {
		ret = PTR_ERR(smem_addr);
		if (ret != -EPROBE_DEFER)
			pr_err("unable to acquire SMEM project entry: %d\n", ret);
		return ret;
	}
	if (smem_size < sizeof(*format)) {
		pr_err("SMEM project entry too small: %zu, expected %zu\n",
		       smem_size, sizeof(*format));
		return -EINVAL;
	}

	WRITE_ONCE(format, smem_addr);
	return 0;
}
EXPORT_SYMBOL(oppo_project_info_init);

void init_project_version(void)
{
	oppo_project_info_init();
}

static ProjectInfoCDTType *oppo_project_info(void)
{
	ProjectInfoCDTType *project = READ_ONCE(format);

	if (!project) {
		if (oppo_project_info_init())
			return NULL;
		project = READ_ONCE(format);
	}

	return project;
}

unsigned int get_project(void)
{
	ProjectInfoCDTType *project = oppo_project_info();

	return project ? project->nproject : 0;
}
EXPORT_SYMBOL(get_project);

unsigned int is_project(OPPO_PROJECT project)
{
	return (get_project() == project ? 1 : 0);
}
EXPORT_SYMBOL(is_project);

unsigned char get_PCB_Version(void)
{
	ProjectInfoCDTType *project = oppo_project_info();

	return project ? project->npcbversion : 0;
}
EXPORT_SYMBOL(get_PCB_Version);

unsigned char get_Modem_Version(void)
{
	ProjectInfoCDTType *project = oppo_project_info();

	return project ? project->nmodem : 0;
}
EXPORT_SYMBOL(get_Modem_Version);

unsigned char get_Operator_Version(void)
{
	ProjectInfoCDTType *project = oppo_project_info();

	return project ? project->noperator : 0;
}
EXPORT_SYMBOL(get_Operator_Version);

unsigned char get_Oppo_Boot_Mode(void)
{
	ProjectInfoCDTType *project = oppo_project_info();

	return project ? project->noppobootmode : 0;
}
EXPORT_SYMBOL(get_Oppo_Boot_Mode);

/* for get which ldo ocp */
static void print_ocp(void)
{
	ProjectInfoCDTType *project = oppo_project_info();

	if (!project)
		return;
	pr_info("ocp: %u %u %u %u\n", project->npmicocp[0],
		project->npmicocp[1], project->npmicocp[2],
		project->npmicocp[3]);
}

static int __init ocplog_init(void)
{
	print_ocp();
	return 0;
}
late_initcall(ocplog_init);

int rpmb_is_enable(void)
{
	static unsigned int rpmbenable;
	void __iomem *rpmb_addr;
	unsigned int rpmbtmp;

	if (rpmbenable)
		return rpmbenable;

	rpmb_addr = ioremap(RPMB_KEY_PROVISIONED, 4);
	if (rpmb_addr) {
		rpmbtmp = __raw_readl(rpmb_addr);
		iounmap(rpmb_addr);
		rpmbenable = (rpmbtmp >> 24) & 0x01;
	} else {
		rpmbenable = 0;
	}
	return rpmbenable;
}
EXPORT_SYMBOL(rpmb_is_enable);

static unsigned int g_serial_id = 0x00;

static unsigned int read_sec_reg(unsigned long reg)
{
	void __iomem *base;
	unsigned int val = 0;

	base = ioremap(reg, 4);
	if (base) {
		val = __raw_readl(base);
		iounmap(base);
	}
	return val;
}

static int project_read_func(struct seq_file *s, void *v)
{
	switch ((uintptr_t)s->private) {
	case PRJ_VERSION:
		seq_printf(s, "%d", get_project());
		break;
	case PRJ_PCB_VERSION:
		seq_printf(s, "%d", get_PCB_Version());
		break;
	case PRJ_MODEM_TYPE:
		seq_printf(s, "%d", get_Modem_Version());
		break;
	case PRJ_OPERATOR_NAME:
		seq_printf(s, "%d", get_Operator_Version());
		break;
	case PRJ_OPPO_BOOTMODE:
		seq_printf(s, "%d", get_Oppo_Boot_Mode());
		break;
	case PRJ_SECURE_TYPE:
		seq_printf(s, "%d", read_sec_reg(OEM_SEC_BOOT_REG));
		break;
	case PRJ_SECURE_STAGE:
		seq_printf(s, "%d", read_sec_reg(OEM_SEC_ENABLE_ANTIROLLBACK_REG));
		break;
	case PRJ_SERIAL_ID:
		seq_printf(s, "0x%x", g_serial_id);
		break;
	case PRJ_OCP: {
		ProjectInfoCDTType *project = oppo_project_info();
		int i;

		if (!project) {
			seq_printf(s, "ocp: unavailable\n");
			break;
		}
		seq_printf(s, "ocp:");
		for (i = 0; i < OCPCOUNTMAX; i++)
			seq_printf(s, " %d", project->npmicocp[i]);
		seq_printf(s, "\n");
		break;
	}
	default:
		break;
	}
	return 0;
}

static int projects_open(struct inode *inode, struct file *file)
{
	return single_open(file, project_read_func, PDE_DATA(inode));
}

static const struct file_operations project_info_fops = {
	.owner		= THIS_MODULE,
	.open		= projects_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init oppo_project_init(void)
{
	struct proc_dir_entry *pentry;
	void __iomem *serial_id_addr;

	serial_id_addr = ioremap(QFPROM_RAW_SERIAL_NUM, 4);
	if (serial_id_addr) {
		g_serial_id = __raw_readl(serial_id_addr);
		iounmap(serial_id_addr);
		printk(KERN_EMERG "serialID 0x%x\n", g_serial_id);
	} else {
		g_serial_id = 0xffffffff;
	}

	oppoVersion = proc_mkdir("oppoVersion", NULL);
	if (!oppoVersion) {
		pr_err("can't create oppoVersion proc\n");
		return -ENOENT;
	}

	pentry = proc_create_data("prjVersion", 0444, oppoVersion,
				  &project_info_fops,
				  (void *)(uintptr_t)PRJ_VERSION);
	if (!pentry)
		goto ERROR_INIT_VERSION;
	pentry = proc_create_data("pcbVersion", 0444, oppoVersion,
				  &project_info_fops,
				  (void *)(uintptr_t)PRJ_PCB_VERSION);
	if (!pentry)
		goto ERROR_INIT_VERSION;
	pentry = proc_create_data("operatorName", 0444, oppoVersion,
				  &project_info_fops,
				  (void *)(uintptr_t)PRJ_OPERATOR_NAME);
	if (!pentry)
		goto ERROR_INIT_VERSION;
	pentry = proc_create_data("modemType", 0444, oppoVersion,
				  &project_info_fops,
				  (void *)(uintptr_t)PRJ_MODEM_TYPE);
	if (!pentry)
		goto ERROR_INIT_VERSION;
	pentry = proc_create_data("oppoBootmode", 0444, oppoVersion,
				  &project_info_fops,
				  (void *)(uintptr_t)PRJ_OPPO_BOOTMODE);
	if (!pentry)
		goto ERROR_INIT_VERSION;
	pentry = proc_create_data("secureType", 0444, oppoVersion,
				  &project_info_fops,
				  (void *)(uintptr_t)PRJ_SECURE_TYPE);
	if (!pentry)
		goto ERROR_INIT_VERSION;
	pentry = proc_create_data("secureStage", 0444, oppoVersion,
				  &project_info_fops,
				  (void *)(uintptr_t)PRJ_SECURE_STAGE);
	if (!pentry)
		goto ERROR_INIT_VERSION;
	pentry = proc_create_data("serialID", 0444, oppoVersion,
				  &project_info_fops,
				  (void *)(uintptr_t)PRJ_SERIAL_ID);
	if (!pentry)
		goto ERROR_INIT_VERSION;
	pentry = proc_create_data("ocp", 0444, oppoVersion,
				  &project_info_fops,
				  (void *)(uintptr_t)PRJ_OCP);
	if (!pentry)
		goto ERROR_INIT_VERSION;

	return 0;

ERROR_INIT_VERSION:
	proc_remove(oppoVersion);
	oppoVersion = NULL;
	return -ENOENT;
}
arch_initcall(oppo_project_init);

MODULE_DESCRIPTION("OPPO project version");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Joshua <gyx@oppo.com>");
