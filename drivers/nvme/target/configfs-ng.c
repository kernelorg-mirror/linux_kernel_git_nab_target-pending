/*
 * Based on target_core_fabric_configfs.c code
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/ctype.h>
#include <target/target_core_base.h>
#include <target/target_core_backend.h>

#include "nvmet.h"

/*
 * NVMf host CIT
 */
static void nvmet_host_release(struct config_item *item)
{
	struct nvmet_host *host = to_host(item);
	struct nvmet_subsys *subsys = host->subsys;

	mutex_lock(&subsys->hosts_mutex);
	list_del_init(&host->node);
	mutex_unlock(&subsys->hosts_mutex);

	kfree(host);
}

static struct configfs_item_operations nvmet_host_item_opts = {
	.release		= nvmet_host_release,
};

static struct config_item_type nvmet_host_type = {
	.ct_item_ops		= &nvmet_host_item_opts,
	.ct_attrs		= NULL,
	.ct_owner		= THIS_MODULE,

};

static struct config_group *nvmet_make_hosts(struct config_group *group,
		const char *name)
{
	struct nvmet_subsys *subsys = ports_to_subsys(&group->cg_item);
	struct nvmet_host *host;

	host = kzalloc(sizeof(*host), GFP_KERNEL);
	if (!host)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&host->node);
	host->subsys = subsys;

	mutex_lock(&subsys->hosts_mutex);
	list_add_tail(&host->node, &subsys->hosts);
	mutex_unlock(&subsys->hosts_mutex);

	config_group_init_type_name(&host->group, name, &nvmet_host_type);

	return &host->group;
}

static void nvmet_drop_hosts(struct config_group *group, struct config_item *item)
{
	config_item_put(item);
}

static struct configfs_group_operations nvmet_hosts_group_ops = {
	.make_group		= nvmet_make_hosts,
	.drop_item		= nvmet_drop_hosts,
};

static struct config_item_type nvmet_hosts_type = {
	.ct_group_ops		= &nvmet_hosts_group_ops,
	.ct_item_ops		= NULL,
	.ct_attrs		= NULL,
	.ct_owner		= THIS_MODULE,
};

/*
 * nvmet_port Generic ConfigFS definitions.
 */
static ssize_t nvmet_port_addr_adrfam_show(struct config_item *item,
		char *page)
{
	switch (to_nvmet_port_binding(item)->disc_addr.adrfam) {
	case NVMF_ADDR_FAMILY_IP4:
		return sprintf(page, "ipv4\n");
	case NVMF_ADDR_FAMILY_IP6:
		return sprintf(page, "ipv6\n");
	case NVMF_ADDR_FAMILY_IB:
		return sprintf(page, "ib\n");
	default:
		return sprintf(page, "\n");
	}
}

static ssize_t nvmet_port_addr_adrfam_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_port_binding *pb = to_nvmet_port_binding(item);

	if (pb->enabled) {
		pr_err("Cannot modify address while enabled\n");
		pr_err("Disable the address before modifying\n");
		return -EACCES;
	}

	if (sysfs_streq(page, "ipv4")) {
		pb->disc_addr.adrfam = NVMF_ADDR_FAMILY_IP4;
	} else if (sysfs_streq(page, "ipv6")) {
		pb->disc_addr.adrfam = NVMF_ADDR_FAMILY_IP6;
	} else if (sysfs_streq(page, "ib")) {
		pb->disc_addr.adrfam = NVMF_ADDR_FAMILY_IB;
	} else {
		pr_err("Invalid value '%s' for adrfam\n", page);
		return -EINVAL;
	}

	return count;
}

CONFIGFS_ATTR(nvmet_port_, addr_adrfam);

static ssize_t nvmet_port_addr_portid_show(struct config_item *item,
		char *page)
{
	struct nvmet_port_binding *pb = to_nvmet_port_binding(item);

	return snprintf(page, PAGE_SIZE, "%d\n",
			le16_to_cpu(pb->disc_addr.portid));
}

static ssize_t nvmet_port_addr_portid_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_port_binding *pb = to_nvmet_port_binding(item);
	u16 portid = 0;

	if (kstrtou16(page, 0, &portid)) {
		pr_err("Invalid value '%s' for portid\n", page);
		return -EINVAL;
	}

	if (pb->enabled) {
		pr_err("Cannot modify address while enabled\n");
		pr_err("Disable the address before modifying\n");
		return -EACCES;
	}
	pb->disc_addr.portid = cpu_to_le16(portid);
	return count;
}

CONFIGFS_ATTR(nvmet_port_, addr_portid);

static ssize_t nvmet_port_addr_traddr_show(struct config_item *item,
		char *page)
{
	struct nvmet_port_binding *pb = to_nvmet_port_binding(item);

	return snprintf(page, PAGE_SIZE, "%s\n",
			pb->disc_addr.traddr);
}

static ssize_t nvmet_port_addr_traddr_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_port_binding *pb = to_nvmet_port_binding(item);

	if (count > NVMF_TRADDR_SIZE) {
		pr_err("Invalid value '%s' for traddr\n", page);
		return -EINVAL;
	}

	if (pb->enabled) {
		pr_err("Cannot modify address while enabled\n");
		pr_err("Disable the address before modifying\n");
		return -EACCES;
	}
	return snprintf(pb->disc_addr.traddr,
			sizeof(pb->disc_addr.traddr), "%s", page);
}

CONFIGFS_ATTR(nvmet_port_, addr_traddr);

static ssize_t nvmet_port_addr_treq_show(struct config_item *item,
		char *page)
{
	switch (to_nvmet_port_binding(item)->disc_addr.treq) {
	case NVMF_TREQ_NOT_SPECIFIED:
		return sprintf(page, "not specified\n");
	case NVMF_TREQ_REQUIRED:
		return sprintf(page, "required\n");
	case NVMF_TREQ_NOT_REQUIRED:
		return sprintf(page, "not required\n");
	default:
		return sprintf(page, "\n");
	}
}

static ssize_t nvmet_port_addr_treq_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_port_binding *pb = to_nvmet_port_binding(item);

	if (pb->enabled) {
		pr_err("Cannot modify address while enabled\n");
		pr_err("Disable the address before modifying\n");
		return -EACCES;
	}

	if (sysfs_streq(page, "not specified")) {
		pb->disc_addr.treq = NVMF_TREQ_NOT_SPECIFIED;
	} else if (sysfs_streq(page, "required")) {
		pb->disc_addr.treq = NVMF_TREQ_REQUIRED;
	} else if (sysfs_streq(page, "not required")) {
		pb->disc_addr.treq = NVMF_TREQ_NOT_REQUIRED;
	} else {
		pr_err("Invalid value '%s' for treq\n", page);
		return -EINVAL;
	}

	return count;
}

CONFIGFS_ATTR(nvmet_port_, addr_treq);

static ssize_t nvmet_port_addr_trsvcid_show(struct config_item *item,
		char *page)
{
	struct nvmet_port_binding *pb = to_nvmet_port_binding(item);

	return snprintf(page, PAGE_SIZE, "%s\n",
			pb->disc_addr.trsvcid);
}

static ssize_t nvmet_port_addr_trsvcid_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_port_binding *pb = to_nvmet_port_binding(item);

	if (count > NVMF_TRSVCID_SIZE) {
		pr_err("Invalid value '%s' for trsvcid\n", page);
		return -EINVAL;
	}
	if (pb->enabled) {
		pr_err("Cannot modify address while enabled\n");
		pr_err("Disable the address before modifying\n");
		return -EACCES;
	}
	return snprintf(pb->disc_addr.trsvcid,
			sizeof(pb->disc_addr.trsvcid), "%s", page);
}

CONFIGFS_ATTR(nvmet_port_, addr_trsvcid);

static ssize_t nvmet_port_addr_trtype_show(struct config_item *item,
		char *page)
{
	switch (to_nvmet_port_binding(item)->disc_addr.trtype) {
	case NVMF_TRTYPE_RDMA:
		return sprintf(page, "rdma\n");
	case NVMF_TRTYPE_LOOP:
		return sprintf(page, "loop\n");
	default:
		return sprintf(page, "\n");
	}
}

static void nvmet_port_init_tsas_rdma(struct nvmet_port_binding *pb)
{
	pb->disc_addr.trtype = NVMF_TRTYPE_RDMA;
	memset(&pb->disc_addr.tsas.rdma, 0, NVMF_TSAS_SIZE);
	pb->disc_addr.tsas.rdma.qptype = NVMF_RDMA_QPTYPE_CONNECTED;
	pb->disc_addr.tsas.rdma.prtype = NVMF_RDMA_PRTYPE_NOT_SPECIFIED;
	pb->disc_addr.tsas.rdma.cms = NVMF_RDMA_CMS_RDMA_CM;
}

static void nvmet_port_init_tsas_loop(struct nvmet_port_binding *pb)
{
	pb->disc_addr.trtype = NVMF_TRTYPE_LOOP;
	memset(&pb->disc_addr.tsas, 0, NVMF_TSAS_SIZE);
}

static ssize_t nvmet_port_addr_trtype_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_port_binding *pb = to_nvmet_port_binding(item);

	if (pb->enabled) {
		pr_err("Cannot modify address while enabled\n");
		pr_err("Disable the address before modifying\n");
		return -EACCES;
	}

	if (sysfs_streq(page, "rdma")) {
		nvmet_port_init_tsas_rdma(pb);
	} else if (sysfs_streq(page, "loop")) {
		nvmet_port_init_tsas_loop(pb);
	} else {
		pr_err("Invalid value '%s' for trtype\n", page);
		return -EINVAL;
	}

	return count;
}

CONFIGFS_ATTR(nvmet_port_, addr_trtype);

static void nvmet_port_disable(struct nvmet_port_binding *pb)
{
	struct nvmet_fabrics_ops *ops = pb->nf_ops;
	struct nvmet_port *port = pb->port;

	if (!ops || !port)
		return;

	ops->remove_port(pb);
	nvmet_put_transport(&pb->disc_addr);
	pb->nf_ops = NULL;

	atomic64_inc(&nvmet_genctr);
}

static ssize_t nvmet_port_enable_show(struct config_item *item, char *page)
{
	struct nvmet_port_binding *pb = to_nvmet_port_binding(item);

	return sprintf(page, "%d\n", pb->enabled);
}

static ssize_t nvmet_port_enable_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_port *port;
	struct nvmet_port_binding *pb = to_nvmet_port_binding(item);
	struct nvmet_fabrics_ops *ops;
	bool enable;
	int rc;

	if (strtobool(page, &enable))
		return -EINVAL;

	if (enable) {
		if (pb->enabled) {
			pr_warn("port already enabled: %d\n",
				pb->disc_addr.trtype);
			goto out;
		}

		ops = nvmet_get_transport(&pb->disc_addr);
		if (IS_ERR(ops))
			return PTR_ERR(ops);

		pb->nf_ops = ops;

		rc = ops->add_port(pb);
		if (rc) {
			nvmet_put_transport(&pb->disc_addr);
			return rc;
		}

		atomic64_inc(&nvmet_genctr);
	} else {
		if (!pb->nf_ops)
			return -EINVAL;

		port = pb->port;
		if (!port)
			return -EINVAL;

		nvmet_port_disable(pb);
	}
out:
	return count;
}

CONFIGFS_ATTR(nvmet_port_, enable);

static struct configfs_attribute *nvmet_port_attrs[] = {
	&nvmet_port_attr_addr_adrfam,
	&nvmet_port_attr_addr_portid,
	&nvmet_port_attr_addr_traddr,
	&nvmet_port_attr_addr_treq,
	&nvmet_port_attr_addr_trsvcid,
	&nvmet_port_attr_addr_trtype,
	&nvmet_port_attr_enable,
	NULL,
};

/*
 * NVMf transport port CIT
 */
static void nvmet_port_release(struct config_item *item)
{
	struct nvmet_port_binding *pb = to_nvmet_port_binding(item);

	nvmet_port_disable(pb);
	kfree(pb);
}

static struct configfs_item_operations nvmet_port_item_ops = {
	.release	= nvmet_port_release,
};

static struct config_item_type nvmet_port_type = {
	.ct_item_ops		= &nvmet_port_item_ops,
	.ct_attrs		= nvmet_port_attrs,
	.ct_owner		= THIS_MODULE,
};

static struct config_group *nvmet_make_ports(struct config_group *group,
		const char *name)
{
	struct nvmet_subsys *subsys = ports_to_subsys(&group->cg_item);
	struct nvmet_port_binding *pb;

	printk("Entering nvmet_make_port %s >>>>>>>>>>>>>>>>>>\n", name);

	pb = kzalloc(sizeof(*pb), GFP_KERNEL);
	if (!pb)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&pb->node);
	pb->nf_subsys = subsys;

	config_group_init_type_name(&pb->group, name, &nvmet_port_type);

	return &pb->group;
}

static void nvmet_drop_ports(struct config_group *group, struct config_item *item)
{
	config_item_put(item);
}

static struct configfs_group_operations nvmet_ports_group_ops = {
	.make_group		= nvmet_make_ports,
	.drop_item		= nvmet_drop_ports,
};

static struct config_item_type nvmet_ports_type = {
	.ct_group_ops		= &nvmet_ports_group_ops,
	.ct_item_ops		= NULL,
	.ct_attrs		= NULL,
	.ct_owner		= THIS_MODULE,
};

/*
 * NVMf namespace <-> /sys/kernel/config/target/core/ backend configfs symlink
 */
static int nvmet_ns_link(struct config_item *ns_ci, struct config_item *dev_ci)
{
	struct nvmet_ns *ns = to_nvmet_ns(ns_ci);
	struct se_device *dev =
		container_of(to_config_group(dev_ci), struct se_device, dev_group);

	if (dev->dev_link_magic != SE_DEV_LINK_MAGIC) {
		pr_err("Bad dev->dev_link_magic, not a valid se_dev_ci pointer:"
		       " %p to struct se_device: %p\n", dev_ci, dev);
		return -EFAULT;
	}

	if (!(dev->dev_flags & DF_CONFIGURED)) {
		pr_err("se_device not configured yet, cannot namespace link\n");
		return -ENODEV;
	}

	if (!dev->transport->sbc_ops) {
		pr_err("se_device does not have sbc_ops, cannot namespace link\n");
		return -ENOSYS;
	}

	// XXX: Pass in struct se_device into nvmet_ns_enable
	return nvmet_ns_enable(ns);
}

static int nvmet_ns_unlink(struct config_item *ns_ci, struct config_item *dev_ci)
{
	struct nvmet_ns *ns = to_nvmet_ns(ns_ci);

	nvmet_ns_disable(ns);
	return 0;
}

static void nvmet_ns_release(struct config_item *item)
{
	struct nvmet_ns *ns = to_nvmet_ns(item);

	nvmet_ns_free(ns);
}

static struct configfs_item_operations nvmet_ns_item_ops = {
	.release		= nvmet_ns_release,
	.allow_link		= nvmet_ns_link,
	.drop_link		= nvmet_ns_unlink,
};

static struct config_item_type nvmet_ns_type = {
	.ct_item_ops		= &nvmet_ns_item_ops,
	.ct_attrs		= NULL,
	.ct_owner		= THIS_MODULE,
};

static struct config_group *nvmet_make_namespace(struct config_group *group,
		const char *name)
{
	struct nvmet_subsys *subsys = namespaces_to_subsys(&group->cg_item);
	struct nvmet_ns *ns;
	int ret;
	u32 nsid;

	ret = kstrtou32(name, 0, &nsid);
	if (ret)
		goto out;

	ret = -EINVAL;
	if (nsid == 0 || nsid == 0xffffffff)
		goto out;

	ret = -ENOMEM;
	ns = nvmet_ns_alloc(subsys, nsid);
	if (!ns)
		goto out;
	config_group_init_type_name(&ns->group, name, &nvmet_ns_type);

	pr_info("adding nsid %d to subsystem %s\n", nsid, subsys->subsysnqn);

	return &ns->group;
out:
	return ERR_PTR(ret);
}

static void nvmet_drop_namespace(struct config_group *group, struct config_item *item)
{
	/*
	 * struct nvmet_ns is released via nvmet_ns_release()
	 */
	config_item_put(item);
}

static struct configfs_group_operations nvmet_namespaces_group_ops = {
	.make_group		= nvmet_make_namespace,
	.drop_item		= nvmet_drop_namespace,
};

static struct config_item_type nvmet_namespaces_type = {
	.ct_group_ops		= &nvmet_namespaces_group_ops,
	.ct_owner		= THIS_MODULE,
};

/*
 * Subsystem structures & folder operation functions below
 */
static void nvmet_subsys_release(struct config_item *item)
{
	struct nvmet_subsys *subsys = to_subsys(item);

	nvmet_subsys_put(subsys);
}

static struct configfs_item_operations nvmet_subsys_item_ops = {
	.release		= nvmet_subsys_release,
};

static struct config_item_type nvmet_subsys_type = {
	.ct_item_ops		= &nvmet_subsys_item_ops,
//	.ct_attrs		= nvmet_subsys_attrs,
	.ct_owner		= THIS_MODULE,
};

static struct config_group *nvmet_make_subsys(struct config_group *group,
		const char *name)
{
	struct nvmet_subsys *subsys;

	if (sysfs_streq(name, NVME_DISC_SUBSYS_NAME)) {
		pr_err("can't create discovery subsystem through configfs\n");
		return ERR_PTR(-EINVAL);
	}

	subsys = nvmet_subsys_alloc(name, NVME_NQN_NVME);
	if (!subsys)
		return ERR_PTR(-ENOMEM);

	config_group_init_type_name(&subsys->group, name, &nvmet_subsys_type);

	config_group_init_type_name(&subsys->namespaces_group,
			"namespaces", &nvmet_namespaces_type);
	configfs_add_default_group(&subsys->namespaces_group, &subsys->group);

	config_group_init_type_name(&subsys->ports_group,
			"ports", &nvmet_ports_type);
	configfs_add_default_group(&subsys->ports_group, &subsys->group);

	config_group_init_type_name(&subsys->hosts_group,
			"hosts", &nvmet_hosts_type);
	configfs_add_default_group(&subsys->hosts_group, &subsys->group);

//	XXX: subsys->allow_any_host hardcoded to true
	subsys->allow_any_host = true;

	return &subsys->group;
}

static void nvmet_drop_subsys(struct config_group *group, struct config_item *item)
{
	/*
	 * struct nvmet_port is releated via nvmet_subsys_release()
	 */
	config_item_put(item);
}

static struct configfs_group_operations nvmet_subsystems_group_ops = {
	.make_group		= nvmet_make_subsys,
	.drop_item		= nvmet_drop_subsys,
};

static struct config_item_type nvmet_subsystems_type = {
	.ct_group_ops		= &nvmet_subsystems_group_ops,
	.ct_owner		= THIS_MODULE,
};

static struct config_group nvmet_subsystems_group;

static struct config_item_type nvmet_root_type = {
	.ct_owner		= THIS_MODULE,
};

static struct configfs_subsystem nvmet_configfs_subsystem = {
	.su_group = {
		.cg_item = {
			.ci_namebuf	= "nvmet",
			.ci_type	= &nvmet_root_type,
		},
	},
};

int __init nvmet_init_configfs(void)
{
	int ret;

	config_group_init(&nvmet_configfs_subsystem.su_group);
	mutex_init(&nvmet_configfs_subsystem.su_mutex);

	config_group_init_type_name(&nvmet_subsystems_group,
			"subsystems", &nvmet_subsystems_type);
	configfs_add_default_group(&nvmet_subsystems_group,
			&nvmet_configfs_subsystem.su_group);

	ret = configfs_register_subsystem(&nvmet_configfs_subsystem);
	if (ret) {
		pr_err("configfs_register_subsystem: %d\n", ret);
		return ret;
	}

	return 0;
}

void __exit nvmet_exit_configfs(void)
{
	configfs_unregister_subsystem(&nvmet_configfs_subsystem);
}
