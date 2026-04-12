/*
 * SystemInfo — OmniRoam HostPC
 * GTK3 desktop utility for Ubuntu 20.04 LTS
 */

#include <errno.h>
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <unistd.h>

#define OS_DISPLAY_NAME "OmniOS-Beta26"
#define OS_SUBTITLE "基于 Ubuntu 开发的发行版"

enum { PAGE_OVERVIEW = 0, PAGE_USAGE = 1 };

typedef struct {
	GtkWidget *stack;
	GtkWidget *overview_text;
	GtkWidget *usage_text;
	guint usage_timer;
	gulong prev_cpu_idle;
	gulong prev_cpu_total;
	int has_prev_cpu;
} AppWidgets;

static int read_proc_line_value(const char *path, const char *key, unsigned long long *out) {
	FILE *f = fopen(path, "r");
	if (!f)
		return -1;
	char line[512];
	size_t keylen = strlen(key);
	while (fgets(line, sizeof line, f)) {
		if (strncmp(line, key, keylen) == 0 && line[keylen] == ':') {
			unsigned long long v = 0ULL;
			if (sscanf(line + keylen + 1, " %llu", &v) == 1) {
				*out = v;
				fclose(f);
				return 0;
			}
		}
	}
	fclose(f);
	return -1;
}

static void cpu_model_name(char *buf, size_t buflen) {
	buf[0] = '\0';
	FILE *f = fopen("/proc/cpuinfo", "r");
	if (!f)
		return;
	char line[512];
	while (fgets(line, sizeof line, f)) {
		const char *p = "model name\t: ";
		if (strncmp(line, p, strlen(p)) == 0) {
			g_strlcpy(buf, line + strlen(p), buflen);
			size_t n = strlen(buf);
			if (n && buf[n - 1] == '\n')
				buf[n - 1] = '\0';
			fclose(f);
			return;
		}
	}
	fclose(f);
}

static void format_kib(unsigned long long kib, char *out, size_t outsz) {
	if (kib >= 1024ULL * 1024ULL * 1024ULL) {
		g_snprintf(out, outsz, "%.2f TiB", (double)kib / (1024.0 * 1024.0 * 1024.0));
	} else if (kib >= 1024ULL * 1024ULL) {
		g_snprintf(out, outsz, "%.2f GiB", (double)kib / (1024.0 * 1024.0));
	} else {
		g_snprintf(out, outsz, "%llu MiB", kib / 1024ULL);
	}
}

static int read_cpu_jiffs(gulong *idle_out, gulong *total_out) {
	FILE *f = fopen("/proc/stat", "r");
	if (!f)
		return -1;
	unsigned long u, n, s, id, io, irq, soft, steal, guest, gn;
	if (fscanf(f, "cpu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu", &u, &n, &s, &id, &io, &irq,
		   &soft, &steal, &guest, &gn) < 4) {
		fclose(f);
		return -1;
	}
	fclose(f);
	*idle_out = id + io;
	*total_out = u + n + s + id + io + irq + soft + steal;
	return 0;
}

static void refresh_overview(GtkTextBuffer *buf) {
	struct utsname un;
	if (uname(&un) != 0) {
		memset(&un, 0, sizeof un);
	}

	char model[256];
	cpu_model_name(model, sizeof model);

	unsigned long long mem_total_kib = 0;
	if (read_proc_line_value("/proc/meminfo", "MemTotal", &mem_total_kib) != 0)
		mem_total_kib = 0;

	char mem_human[64];
	format_kib(mem_total_kib, mem_human, sizeof mem_human);

	long nproc = sysconf(_SC_NPROCESSORS_ONLN);
	if (nproc < 1)
		nproc = 1;

	GString *s = g_string_new(NULL);
	g_string_append_printf(s, "操作系统\n  %s\n  %s\n\n", OS_DISPLAY_NAME, OS_SUBTITLE);
	g_string_append_printf(s, "内核\n  %s %s\n\n", un.sysname, un.release);
	g_string_append_printf(s, "主机名\n  %s\n\n", un.nodename);
	g_string_append_printf(s, "架构\n  %s\n\n", un.machine);
	g_string_append_printf(s, "逻辑 CPU 数\n  %ld\n\n", nproc);
	if (model[0])
		g_string_append_printf(s, "处理器\n  %s\n\n", model);
	else
		g_string_append(s, "处理器\n  （未能读取 /proc/cpuinfo）\n\n");
	g_string_append_printf(s, "物理内存（总量）\n  %s\n", mem_human);

	gtk_text_buffer_set_text(buf, s->str, (gint)s->len);
	g_string_free(s, TRUE);
}

static void refresh_usage(GtkTextBuffer *buf, AppWidgets *w) {
	gulong idle, total;
	if (read_cpu_jiffs(&idle, &total) != 0) {
		gtk_text_buffer_set_text(buf, "无法读取 CPU 状态。", -1);
		return;
	}

	double cpu_pct = 0.0;
	if (w->has_prev_cpu && total > w->prev_cpu_total) {
		gulong didle = idle - w->prev_cpu_idle;
		gulong dtotal = total - w->prev_cpu_total;
		if (dtotal > 0) {
			double busy = (double)(dtotal - didle);
			cpu_pct = (busy / (double)dtotal) * 100.0;
			if (cpu_pct < 0.0)
				cpu_pct = 0.0;
			if (cpu_pct > 100.0)
				cpu_pct = 100.0;
		}
	}
	w->prev_cpu_idle = idle;
	w->prev_cpu_total = total;
	w->has_prev_cpu = 1;

	unsigned long long mem_total = 0, mem_avail = 0;
	read_proc_line_value("/proc/meminfo", "MemTotal", &mem_total);
	read_proc_line_value("/proc/meminfo", "MemAvailable", &mem_avail);

	double mem_used_pct = 0.0;
	unsigned long long mem_used_kib = 0;
	if (mem_total > 0) {
		if (mem_avail <= mem_total)
			mem_used_kib = mem_total - mem_avail;
		mem_used_pct = (double)mem_used_kib * 100.0 / (double)mem_total;
	}

	char mt[64], ma[64], mu[64];
	format_kib(mem_total, mt, sizeof mt);
	format_kib(mem_avail, ma, sizeof ma);
	format_kib(mem_used_kib, mu, sizeof mu);

	struct statvfs st;
	char disk_line[256];
	disk_line[0] = '\0';
	if (statvfs("/", &st) == 0) {
		unsigned long long blocks = (unsigned long long)st.f_blocks;
		unsigned long long bavail = (unsigned long long)st.f_bavail;
		unsigned long long fr = (unsigned long long)st.f_frsize;
		unsigned long long total_b = blocks * fr;
		unsigned long long free_b = bavail * fr;
		unsigned long long used_b = total_b > free_b ? total_b - free_b : 0ULL;
		double disk_pct = total_b > 0 ? (double)used_b * 100.0 / (double)total_b : 0.0;
		char tbuf[64], ubuf[64];
		format_kib(total_b / 1024ULL, tbuf, sizeof tbuf);
		format_kib(used_b / 1024ULL, ubuf, sizeof ubuf);
		g_snprintf(disk_line, sizeof disk_line,
			   "根分区 /\n  已用 %s / 共 %s  （约 %.1f%%）\n", ubuf, tbuf, disk_pct);
	} else {
		g_snprintf(disk_line, sizeof disk_line, "根分区 /\n  无法读取（%s）\n", strerror(errno));
	}

	GString *g = g_string_new(NULL);
	g_string_append_printf(g, "CPU 使用率\n  约 %.1f%%\n  （相对上一采样间隔）\n\n", cpu_pct);
	g_string_append_printf(g, "内存\n  已用 %s / 共 %s  （约 %.1f%%）\n  可用（MemAvailable）约 %s\n\n",
			       mu, mt, mem_used_pct, ma);
	g_string_append(g, disk_line);

	gtk_text_buffer_set_text(buf, g->str, (gint)g->len);
	g_string_free(g, TRUE);
}

static gboolean usage_timer_cb(gpointer data) {
	AppWidgets *w = (AppWidgets *)data;
	GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(w->usage_text));
	refresh_usage(buf, w);
	return G_SOURCE_CONTINUE;
}

static void on_win_destroy(GtkWidget *widget, gpointer user_data) {
	(void)widget;
	AppWidgets *ww = (AppWidgets *)user_data;
	if (ww->usage_timer) {
		g_source_remove(ww->usage_timer);
		ww->usage_timer = 0;
	}
}

static void on_sidebar_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
	(void)box;
	AppWidgets *w = (AppWidgets *)user_data;
	if (!row)
		return;
	int idx = gtk_list_box_row_get_index(row);
	if (idx == PAGE_OVERVIEW) {
		gtk_stack_set_visible_child_name(GTK_STACK(w->stack), "overview");
		if (w->usage_timer) {
			g_source_remove(w->usage_timer);
			w->usage_timer = 0;
		}
		GtkTextBuffer *ob = gtk_text_view_get_buffer(GTK_TEXT_VIEW(w->overview_text));
		refresh_overview(ob);
	} else if (idx == PAGE_USAGE) {
		gtk_stack_set_visible_child_name(GTK_STACK(w->stack), "usage");
		w->has_prev_cpu = 0;
		GtkTextBuffer *ub = gtk_text_view_get_buffer(GTK_TEXT_VIEW(w->usage_text));
		refresh_usage(ub, w);
		if (w->usage_timer)
			g_source_remove(w->usage_timer);
		w->usage_timer = g_timeout_add(1000, usage_timer_cb, w);
	}
}

static GtkWidget *make_sidebar(void) {
	GtkWidget *frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);

	GtkWidget *list = gtk_list_box_new();
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_BROWSE);
	gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(list), TRUE);

	GtkWidget *r1 = gtk_label_new("系统信息");
	gtk_widget_set_halign(r1, GTK_ALIGN_START);
	gtk_widget_set_margin_start(r1, 12);
	gtk_widget_set_margin_end(r1, 12);
	gtk_widget_set_margin_top(r1, 10);
	gtk_widget_set_margin_bottom(r1, 10);
	gtk_container_add(GTK_CONTAINER(list), r1);

	GtkWidget *r2 = gtk_label_new("资源占用");
	gtk_widget_set_halign(r2, GTK_ALIGN_START);
	gtk_widget_set_margin_start(r2, 12);
	gtk_widget_set_margin_end(r2, 12);
	gtk_widget_set_margin_top(r2, 10);
	gtk_widget_set_margin_bottom(r2, 10);
	gtk_container_add(GTK_CONTAINER(list), r2);

	gtk_container_add(GTK_CONTAINER(frame), list);
	gtk_widget_set_size_request(frame, 160, -1);
	return frame;
}

static GtkWidget *make_scrolled_text_view(void) {
	GtkWidget *tv = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(tv), FALSE);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tv), GTK_WRAP_WORD_CHAR);
	gtk_widget_set_margin_start(tv, 12);
	gtk_widget_set_margin_end(tv, 12);
	gtk_widget_set_margin_top(tv, 12);
	gtk_widget_set_margin_bottom(tv, 12);

	GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(sw), tv);
	return sw;
}

static void on_app_activate(GtkApplication *app, gpointer user_data) {
	(void)user_data;
	AppWidgets *w = g_new0(AppWidgets, 1);

	GtkWidget *win = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(win), "SystemInfo");
	gtk_window_set_icon_name(GTK_WINDOW(win), "systeminfo");
	gtk_window_set_default_size(GTK_WINDOW(win), 720, 480);
	gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_CENTER);

	GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_container_add(GTK_CONTAINER(win), hbox);

	GtkWidget *sidebar_outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start(GTK_BOX(hbox), sidebar_outer, FALSE, FALSE, 0);

	GtkWidget *title = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(title), "<b>SystemInfo</b>");
	gtk_widget_set_margin_top(title, 12);
	gtk_widget_set_margin_bottom(title, 8);
	gtk_widget_set_margin_start(title, 12);
	gtk_widget_set_margin_end(title, 12);
	gtk_box_pack_start(GTK_BOX(sidebar_outer), title, FALSE, FALSE, 0);

	GtkWidget *sidebar = make_sidebar();
	gtk_box_pack_start(GTK_BOX(sidebar_outer), sidebar, TRUE, TRUE, 0);

	w->stack = gtk_stack_new();
	gtk_stack_set_transition_type(GTK_STACK(w->stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
	gtk_box_pack_start(GTK_BOX(hbox), w->stack, TRUE, TRUE, 0);

	GtkWidget *ov_sw = make_scrolled_text_view();
	w->overview_text = gtk_bin_get_child(GTK_BIN(ov_sw));
	gtk_stack_add_named(GTK_STACK(w->stack), ov_sw, "overview");

	GtkWidget *us_sw = make_scrolled_text_view();
	w->usage_text = gtk_bin_get_child(GTK_BIN(us_sw));
	gtk_stack_add_named(GTK_STACK(w->stack), us_sw, "usage");

	GtkWidget *list = gtk_bin_get_child(GTK_BIN(sidebar));
	g_signal_connect(list, "row-activated", G_CALLBACK(on_sidebar_row_activated), w);

	gtk_widget_show_all(win);

	GtkListBoxRow *first = gtk_list_box_get_row_at_index(GTK_LIST_BOX(list), 0);
	if (first)
		gtk_list_box_select_row(GTK_LIST_BOX(list), first);

	GtkTextBuffer *ob = gtk_text_view_get_buffer(GTK_TEXT_VIEW(w->overview_text));
	refresh_overview(ob);

	g_signal_connect(win, "destroy", G_CALLBACK(on_win_destroy), w);
	g_object_set_data_full(G_OBJECT(win), "app-widgets", w, (GDestroyNotify)g_free);
}

int main(int argc, char **argv) {
	GtkApplication *app = gtk_application_new("com.omniroam.systeminfo", G_APPLICATION_FLAGS_NONE);
	g_signal_connect(app, "activate", G_CALLBACK(on_app_activate), NULL);
	int status = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);
	return status;
}
