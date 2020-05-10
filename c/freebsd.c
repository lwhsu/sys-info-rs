#include <kvm.h>
#include <sys/param.h>
//#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/mount.h>
#include <sys/user.h>

#include <stdlib.h>
#include <string.h>
#include <paths.h>
#include <fcntl.h>
#include <unistd.h>

#include "info.h"

#define LEN 20

/* Internal Declarations */
static size_t regetmntinfo(struct statfs **mntbufp,
			   long mntsize, const char **vfslist);
static const char **makevfslist(char *fslist);
static int checkvfsname(const char *vfsname, const char **vfslist);
static char *makenetvfslist(void);

static int skipvfs;

/* Get information */
const char *get_os_type(void) {
	char *s, buf[LEN];
	size_t len;
	int mib[2];

	mib[0] = CTL_KERN;
	mib[1] = KERN_OSTYPE;
	s = malloc(LEN);
	len = sizeof(buf);

	if (sysctl(mib, 2, buf, &len, NULL, 0) == -1)
		strncpy(s, "FreeBSD", len);
	strncpy(s, buf, len);

	return s;
}

const char *get_os_release(void) {
	char *s, buf[LEN];
	size_t len;
	int mib[2];

	mib[0] = CTL_KERN;
	mib[1] = KERN_OSRELEASE;
	s = malloc(LEN);
	len = sizeof(buf);

	if (sysctl(mib, 2, buf, &len, NULL, 0) == -1)
		strncpy(s, "Unknown", len);
	strncpy(s, buf, len);

	return s;
}

unsigned int get_cpu_num(void) {
	unsigned int num;
	int mib[2];
	size_t len;

	mib[0] = CTL_HW;
	mib[1] = HW_NCPU;
	len = sizeof(num);

	if (sysctl(mib, 2, &num, &len, NULL, 0) == -1 || !len)
		num = 1;

	return num;
}

unsigned long get_cpu_speed(void) {
	int speed;
	size_t len;

	len = sizeof(speed);
	sysctlbyname("dev.cpu.0.freq", &speed, &len, NULL, 0);

	return speed;
}

unsigned long get_proc_total(void) {
	kvm_t *kd = NULL;
	struct kinfo_proc *pbase = NULL;
	int nproc;

	kd = kvm_open(NULL, _PATH_DEVNULL, NULL, O_RDONLY, NULL);
	if (!kd)
		return 0;

	pbase = kvm_getprocs(kd, KERN_PROC_PROC, 0, &nproc);
	if (!pbase) {
		kvm_close(kd);
		return 0;
	}

	kvm_close(kd);

	return nproc;
}

MemInfo get_mem_info(void) {
	kvm_t *kd = NULL;
	kd = kvm_open(NULL, _PATH_DEVNULL, NULL, O_RDONLY, NULL);
	if (!kd) {
		//XXX
	}

	struct kvm_swap kswap[16];
	static struct kvm_swap swtot;
	int i, n;

	int pagesize;
	pagesize = getpagesize();

	n = kvm_getswapinfo(kd, kswap, sizeof kswap / sizeof kswap[0],
	    SWIF_DEV_PREFIX);

	for (i = 0; i < n; ++i) {
		swtot.ksw_total += kswap[i].ksw_total;
		swtot.ksw_used += kswap[i].ksw_used;
	}

	//FIXME
//	static unsigned long long size = 0;
//	size_t len;
//	int mib[2];
//	vm_statistics_data_t vm_stat;
//	mach_msg_type_number_t count = HOST_VM_INFO_COUNT;
	MemInfo mi;

//	if (size == 0) {
//		mib[0] = CTL_HW;
//		mib[1] = HW_MEMSIZE;
//		len = sizeof(size);
//		sysctl(mib, 2, &size, &len, NULL, 0);
//		size /= 1024;
//	}
//
//	host_statistics(mach_host_self(), HOST_VM_INFO,
//				(host_info_t)&vm_stat, &count);

//	mi.total       = size;
//	mi.avail       = vm_stat.active_count * PAGE_SIZE / 1024;
//	mi.free        = vm_stat.free_count * PAGE_SIZE / 1024;
	mi.total       = 0;
	mi.avail       = 0;
	mi.free        = 0;

	mi.buffers     = 0;
	mi.cached      = 0;

	mi.swap_total  = (unsigned long long)swtot.ksw_total * pagesize / 1024;
	mi.swap_free   = (unsigned long long)(swtot.ksw_total - swtot.ksw_used) * pagesize / 1024;

	return mi;
}

DiskInfo get_disk_info(void) {
	//FIXME
	DiskInfo di;
	struct statfs *mntbuf;
	const char **vfslist;
	char *str;
	size_t i, mntsize;
	size_t used, availblks;
	const double reported_units = 1e9;
	float pct;
	float most_full = 0.0;
	double toru, dtotal = 0.0, dfree = 0.0;

	str = makenetvfslist();
	vfslist = makevfslist(str);
	free(str);

	mntsize = getmntinfo(&mntbuf, MNT_NOWAIT);
	mntsize = regetmntinfo(&mntbuf, mntsize, vfslist);

	for (i = 0; i < mntsize; i++) {
		if ((mntbuf[i].f_flags & MNT_IGNORE) == 0) {
			used = mntbuf[i].f_blocks - mntbuf[i].f_bfree;
			availblks = mntbuf[i].f_bavail + used;
			pct = (availblks == 0 ? 100.0 :
			       (double)used / (double)availblks * 100.0);
			if (pct > most_full)
				most_full = pct;
			toru = reported_units / mntbuf[i].f_bsize;
			dtotal += mntbuf[i].f_blocks / toru;
			dfree += mntbuf[i].f_bavail / toru;
		}
	}

	free(vfslist);
	di.total = dtotal * 1000000;
	di.free = dfree * 1000000;
	return di;
}

/* Internal definitions */
const char **makevfslist(char *fslist) {
	//FIXME
	const char **av;
	int i;
	char *nextcp;

	if (fslist == NULL)
		return (NULL);
	if (fslist[0] == 'n' && fslist[1] == 'o') {
		fslist += 2;
		skipvfs = 1;
	}
	for (i = 0, nextcp = fslist; *nextcp; nextcp++)
		if (*nextcp == ',')
			i++;
	if ((av = (const char**)malloc((size_t)(i + 2) * sizeof(char *))) == NULL) {
		return (NULL);
	}
	nextcp = fslist;
	i = 0;
	av[i++] = nextcp;
	while ((nextcp = strchr(nextcp, ',')) != NULL) {
		*nextcp++ = '\0';
		av[i++] = nextcp;
	}
	av[i++] = NULL;

	return (av);

}

size_t regetmntinfo(struct statfs **mntbufp, long mntsize,
		    const char **vfslist) {
	//FIXME
	int i, j;
	struct statfs *mntbuf;

	if (vfslist == NULL)
		return (getmntinfo(mntbufp, MNT_WAIT));

	mntbuf = *mntbufp;
	for (j = 0, i = 0; i < mntsize; i++) {
		if (checkvfsname(mntbuf[i].f_fstypename, vfslist))
			continue;
		(void)statfs(mntbuf[i].f_mntonname,&mntbuf[j]);
		j++;
	}
	return (j);
}

int checkvfsname(const char *vfsname, const char **vfslist) {
	//FIXME

	if (vfslist == NULL)
		return (0);
	while (*vfslist != NULL) {
		if (strcmp(vfsname, *vfslist) == 0)
			return (skipvfs);
		++vfslist;
	}
	return (!skipvfs);
}

char *makenetvfslist(void){
	//FIXME
	char *str, *strptr, **listptr;
	int mib[4], maxvfsconf, cnt=0, i;
	size_t miblen;
	struct vfsconf vfc;

	mib[0] = CTL_VFS;
	mib[1] = VFS_GENERIC;
	mib[2] = VFS_MAXTYPENUM;
	miblen=sizeof(maxvfsconf);
	if (sysctl(mib, 3, &maxvfsconf, &miblen, NULL, 0)) {
		return (NULL);
	}

	if ((listptr = (char**)malloc(sizeof(char*)*maxvfsconf)) == NULL) {
		return (NULL);
	}

	miblen = sizeof (struct vfsconf);
	mib[2] = VFS_CONF;
	for (i = 0; i < maxvfsconf; i++) {
		mib[3] = i;
		if (sysctl(mib, 4, &vfc, &miblen, NULL, 0) == 0) {
			if (!(vfc.vfc_flags & MNT_LOCAL)) {
				listptr[cnt++] = strdup(vfc.vfc_name);
				if (listptr[cnt-1] == NULL) {
					return (NULL);
				}
			}
		}
	}

	if (cnt == 0 ||
	    (str = (char*)malloc(sizeof(char) * (32 * cnt + cnt + 2))) == NULL) {
		free(listptr);
		return (NULL);
	}

	*str = 'n';
	*(str + 1) = 'o';
	for (i = 0, strptr = str + 2; i < cnt; i++, strptr++) {
		strncpy(strptr, listptr[i], 32);
		strptr += strlen(listptr[i]);
		*strptr = ',';
		free(listptr[i]);
	}
	*(--strptr) = '\0';
	free(listptr);
	return (str);
}