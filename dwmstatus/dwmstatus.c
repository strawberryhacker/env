/*
 * Copy me if you can.
 * by 20h
 */

#define _BSD_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/statvfs.h>
#include <sys/vfs.h>

#include <X11/Xlib.h>

char *tzargentina = "America/Buenos_Aires";
char *tzutc = "UTC";
char *tzberlin = "Europe/Berlin";

static Display *dpy;

char *
smprintf(char *fmt, ...)
{
	va_list fmtargs;
	char *ret;
	int len;

	va_start(fmtargs, fmt);
	len = vsnprintf(NULL, 0, fmt, fmtargs);
	va_end(fmtargs);

	ret = malloc(++len);
	if (ret == NULL) {
		perror("malloc");
		exit(1);
	}

	va_start(fmtargs, fmt);
	vsnprintf(ret, len, fmt, fmtargs);
	va_end(fmtargs);

	return ret;
}

void
settz(char *tzname)
{
	setenv("TZ", tzname, 1);
}

char *
mktimes(char *fmt, char *tzname)
{
	char buf[129];
	time_t tim;
	struct tm *timtm;

	settz(tzname);
	tim = time(NULL);
	timtm = localtime(&tim);
	if (timtm == NULL)
		return smprintf("");

	if (!strftime(buf, sizeof(buf)-1, fmt, timtm)) {
		fprintf(stderr, "strftime == 0\n");
		return smprintf("");
	}

	return smprintf("%s", buf);
}

void
setstatus(char *str)
{
	XStoreName(dpy, DefaultRootWindow(dpy), str);
	XSync(dpy, False);
}

char *
loadavg(void)
{
	double avgs[3];

	if (getloadavg(avgs, 3) < 0)
		return smprintf("");

	return smprintf("%.2f %.2f %.2f", avgs[0], avgs[1], avgs[2]);
}

char *
cpuload(void)
{
	static unsigned long long prev_total = 0;
	static unsigned long long prev_idle = 0;

	unsigned long long user, nice, system, idle;
	unsigned long long iowait, irq, softirq, steal;
	unsigned long long total, idle_total;
	unsigned long long delta_total, delta_idle;

	FILE *fp = fopen("/proc/stat", "r");
	if (!fp)
		return NULL;

	if (fscanf(fp,
		"cpu %llu %llu %llu %llu %llu %llu %llu %llu",
		&user, &nice, &system, &idle,
		&iowait, &irq, &softirq, &steal) != 8) {
		fclose(fp);
		return NULL;
	}

	fclose(fp);

	total = user + nice + system + idle +
	        iowait + irq + softirq + steal;

	idle_total = idle + iowait;

	char *buf = malloc(16);
	if (!buf)
		return NULL;

	if (prev_total == 0) {
		prev_total = total;
		prev_idle = idle_total;
		snprintf(buf, 16, " N/A");
		return buf;
	}

	delta_total = total - prev_total;
	delta_idle  = idle_total - prev_idle;

	double usage = 0.0;
	if (delta_total)
		usage = 100.0 * (delta_total - delta_idle) / delta_total;

	prev_total = total;
	prev_idle = idle_total;

	snprintf(buf, 16, "%3.0f%%", usage);
	return buf;
}

char *
memusage(void)
{
	unsigned long long total = 0;
	unsigned long long available = 0;
	char key[64];
	unsigned long long value;
	char unit[32];

	FILE *fp = fopen("/proc/meminfo", "r");
	if (!fp)
		return NULL;

	while (fscanf(fp, "%63s %llu %31s", key, &value, unit) == 3) {
		if (strcmp(key, "MemTotal:") == 0)
			total = value;
		else if (strcmp(key, "MemAvailable:") == 0)
			available = value;

		if (total && available)
			break;
	}

	fclose(fp);

	if (!total)
		return NULL;

	double percent =
		100.0 * (double)(total - available) / (double)total;

	char *buf = malloc(16);
	if (!buf)
		return NULL;

	snprintf(buf, 16, "%3.0f%%", percent);
	return buf;
}

char *
diskusage(const char *path)
{
	struct statfs s;
	char *buf;
	unsigned long long total, free;
	double used_pct;

	if (statfs(path, &s) != 0)
		return NULL;

	total = (unsigned long long)s.f_blocks * s.f_bsize;
	free  = (unsigned long long)s.f_bavail * s.f_bsize;

	used_pct = 100.0 * (1.0 - (double)free / (double)total);

	buf = malloc(64);
	if (!buf)
		return NULL;

	snprintf(buf, 64, "%3.0f%%", used_pct);
	return buf;
}

char *
readfile(char *base, char *file)
{
	char *path, line[513];
	FILE *fd;

	memset(line, 0, sizeof(line));

	path = smprintf("%s/%s", base, file);
	fd = fopen(path, "r");
	free(path);
	if (fd == NULL)
		return NULL;

	if (fgets(line, sizeof(line)-1, fd) == NULL) {
		fclose(fd);
		return NULL;
	}
	fclose(fd);

	return smprintf("%s", line);
}

char *
getbattery(char *base)
{
	char *co, status;
	int descap, remcap;

	descap = -1;
	remcap = -1;

	co = readfile(base, "present");
	if (co == NULL)
		return smprintf("");
	if (co[0] != '1') {
		free(co);
		return smprintf("not present");
	}
	free(co);

	co = readfile(base, "charge_full_design");
	if (co == NULL) {
		co = readfile(base, "energy_full_design");
		if (co == NULL)
			return smprintf("");
	}
	sscanf(co, "%d", &descap);
	free(co);

	co = readfile(base, "charge_now");
	if (co == NULL) {
		co = readfile(base, "energy_now");
		if (co == NULL)
			return smprintf("");
	}
	sscanf(co, "%d", &remcap);
	free(co);

	co = readfile(base, "status");
	if (!strncmp(co, "Discharging", 11)) {
		status = '-';
	} else if(!strncmp(co, "Charging", 8)) {
		status = '+';
	} else {
		status = '?';
	}

	if (remcap < 0 || descap < 0)
		return smprintf("invalid");

	return smprintf("%.0f%%%c", ((float)remcap / (float)descap) * 100, status);
}

char *
gettemperature(char *base, char *sensor)
{
	char *co;

	co = readfile(base, sensor);
	if (co == NULL)
		return smprintf("");
	return smprintf("%02.0f°C", atof(co) / 1000);
}

char *
execscript(char *cmd)
{
	FILE *fp;
	char retval[1025], *rv;

	memset(retval, 0, sizeof(retval));

	fp = popen(cmd, "r");
	if (fp == NULL)
		return smprintf("");

	rv = fgets(retval, sizeof(retval), fp);
	pclose(fp);
	if (rv == NULL)
		return smprintf("");
	retval[strlen(retval)-1] = '\0';

	return smprintf("%s", retval);
}

int
main(void)
{
	char *status;
	char *timez;
	char *kbmap;
	char* load;
	char* disk;
	char* mem;

	if (!(dpy = XOpenDisplay(NULL))) {
		fprintf(stderr, "dwmstatus: cannot open display.\n");
		return 1;
	}

	for (;;usleep(500000)) {
		load = cpuload();
		mem = memusage();
		timez = mktimes("Week %W | %A | %d %b %Y | %H:%M:%S", tzberlin);
		kbmap = execscript("setxkbmap -query | grep layout | cut -d':' -f 2- | tr -d ' '");
		disk = diskusage("/home");
		status = smprintf("| cpu %s | ram %s | disk %s | %s | %s ", load, mem, disk, kbmap, timez);
		setstatus(status);

		free(load);
		free(kbmap);
		free(disk);
		free(mem);
		free(timez);
		free(status);
	}

	XCloseDisplay(dpy);
	return 0;
}

