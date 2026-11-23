/*
 * Plume Void — pastilles de fenêtre pour les applis GTK 3 (gestionnaire de fichiers PCManFM) :
 *   rouge = fermer · jaune = réduire (rangée de côté) · verte = plein écran (agrandir / revenir)
 * Mêmes couleurs et mêmes gestes que les applis Plume (term/fenetre.h).
 *
 * Module GTK chargé par GTK3_MODULES=plume-pastilles (/etc/profile.d/plume-void.sh).
 * Il ne fait rien dans les autres applis : seules celles de PF_APPLIS reçoivent les pastilles.
 * Les boutons GTK « réduire » / « agrandir » ne marchent pas sous sway (il les ignore) :
 * la jaune passe donc par swaymsg, la verte par le plein écran, que sway accepte.
 */
#include <gtk/gtk.h>
#include <string.h>

static const char *PF_APPLIS[] = { "pcmanfm", NULL };

static const char *PF_CSS =
	"headerbar.plume-titre { min-height: 30px; padding: 0 10px; }"
	"headerbar.plume-titre .title { font-weight: bold; }"
	"button.plume-pastille { min-width: 13px; min-height: 13px; padding: 0; margin: 0 3px;"
	"  border-radius: 50%; border-width: 1px; border-style: solid; box-shadow: none;"
	"  background-image: none; font-size: 9px; font-weight: bold; color: transparent; }"
	"button.plume-pastille label { padding: 0; margin: 0; min-height: 0; min-width: 0; }"
	"button.plume-rouge { background-color: #ff5f57; border-color: #e0443e; }"
	"button.plume-jaune { background-color: #febc2e; border-color: #dea123; }"
	"button.plume-verte { background-color: #28c840; border-color: #1aab29; }"
	"box.plume-pastilles:hover button.plume-rouge { color: #4d0000; }"
	"box.plume-pastilles:hover button.plume-jaune { color: #995700; }"
	"box.plume-pastilles:hover button.plume-verte { color: #006500; }";

static void pf_fermer(GtkButton *b, GtkWindow *w)
{
	(void)b;
	gtk_window_close(w);
}

static void pf_reduire(GtkButton *b, GtkWindow *w)
{
	(void)b; (void)w;
	/* la fenêtre cliquée est celle qui a le focus ; on la retrouve dans le dock ou avec Super+moins */
	char *argv[] = { "swaymsg", "-q", "move scratchpad", NULL };
	g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
		      NULL, NULL, NULL, NULL);
}

static void pf_plein_ecran(GtkButton *b, GtkWindow *w)
{
	(void)b;
	GdkWindow *g = gtk_widget_get_window(GTK_WIDGET(w));
	if (g && (gdk_window_get_state(g) & GDK_WINDOW_STATE_FULLSCREEN))
		gtk_window_unfullscreen(w);
	else
		gtk_window_fullscreen(w);
}

static GtkWidget *pf_bouton(GtkWindow *w, const char *signe, const char *couleur, const char *bulle, GCallback cb)
{
	GtkWidget *b = gtk_button_new_with_label(signe);
	GtkStyleContext *sc = gtk_widget_get_style_context(b);
	gtk_style_context_add_class(sc, "plume-pastille");
	gtk_style_context_add_class(sc, couleur);
	gtk_widget_set_tooltip_text(b, bulle);
	gtk_widget_set_valign(b, GTK_ALIGN_CENTER);
	gtk_widget_set_can_focus(b, FALSE);
	g_signal_connect(b, "clicked", cb, w);
	return b;
}

static void pf_titre(GObject *o, GParamSpec *ps, GtkHeaderBar *hb)
{
	(void)ps;
	const char *t = gtk_window_get_title(GTK_WINDOW(o));
	gtk_header_bar_set_title(hb, t ? t : "");
}

static void pf_css(void)
{
	static gboolean fait;
	GdkScreen *s = gdk_screen_get_default();
	if (fait || !s)
		return;
	fait = TRUE;
	GtkCssProvider *p = gtk_css_provider_new();
	gtk_css_provider_load_from_data(p, PF_CSS, -1, NULL);
	gtk_style_context_add_provider_for_screen(s, GTK_STYLE_PROVIDER(p), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
	g_object_unref(p);
}

static void pf_habiller(GtkWindow *w)
{
	if (g_object_get_data(G_OBJECT(w), "plume-pastilles"))
		return;
	g_object_set_data(G_OBJECT(w), "plume-pastilles", GINT_TO_POINTER(1));
	pf_css();

	GtkWidget *hb = gtk_header_bar_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(hb), "plume-titre");
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(hb), FALSE);
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(box), "plume-pastilles");
	gtk_box_pack_start(GTK_BOX(box), pf_bouton(w, "×", "plume-rouge", "Fermer", G_CALLBACK(pf_fermer)), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), pf_bouton(w, "−", "plume-jaune", "Réduire", G_CALLBACK(pf_reduire)), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), pf_bouton(w, "⤢", "plume-verte", "Plein écran", G_CALLBACK(pf_plein_ecran)), FALSE, FALSE, 0);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(hb), box);
	pf_titre(G_OBJECT(w), NULL, GTK_HEADER_BAR(hb));
	g_signal_connect(w, "notify::title", G_CALLBACK(pf_titre), hb);
	gtk_widget_show_all(hb);
	gtk_window_set_titlebar(w, hb);
}

/* Le contenu d'une fenêtre y est ajouté avant son affichage : c'est le bon moment pour
 * poser la barre de titre (après l'affichage, GTK devrait recréer la fenêtre). */
static gboolean pf_ajout(GSignalInvocationHint *h, guint n, const GValue *v, gpointer d)
{
	(void)h; (void)n; (void)d;
	GObject *o = g_value_get_object(&v[0]);
	if (!GTK_IS_WINDOW(o) || GTK_IS_DIALOG(o) || gtk_widget_get_realized(GTK_WIDGET(o)))
		return TRUE;
	GtkWindow *w = GTK_WINDOW(o);
	if (gtk_window_get_window_type(w) != GTK_WINDOW_TOPLEVEL || gtk_window_get_titlebar(w) ||
	    gtk_window_get_type_hint(w) != GDK_WINDOW_TYPE_HINT_NORMAL || gtk_window_get_transient_for(w))
		return TRUE;
	pf_habiller(w);
	return TRUE;
}

G_MODULE_EXPORT void gtk_module_init(gint *argc, gchar ***argv)
{
	const char *nom = g_get_prgname();
	char *base = NULL;
	if (!nom && argv && *argv && (*argv)[0])
		nom = base = g_path_get_basename((*argv)[0]);
	(void)argc;
	gboolean oui = FALSE;
	for (int i = 0; nom && PF_APPLIS[i]; i++)
		oui |= strcmp(nom, PF_APPLIS[i]) == 0;
	g_free(base);
	if (!oui)
		return;
	/* la classe doit être chargée pour que son signal « add » existe (référence gardée) */
	g_type_class_ref(GTK_TYPE_CONTAINER);
	g_signal_add_emission_hook(g_signal_lookup("add", GTK_TYPE_CONTAINER), 0, pf_ajout, NULL, NULL);
}
