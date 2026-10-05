/* Two languages: English, as written in the code, and French. np_t() turns an
 * English text into its French one when French is chosen (Settings > Language),
 * and returns it unchanged otherwise or when no translation is listed. Source files are UTF-8; the
 * accents are drawn by gfx.c. */
#include <string.h>
#include "np.h"

bool np_french;

typedef struct {
  const char *en, *fr;
} pair_t;

static const pair_t pairs[] = {
    /* home */
    {"Settings", "Réglages"},
    {"Reset, uninstall, more", "Effacer, supprimer, plus"},
    {"Set the time", "Régler l'heure"},
    {"Left/Right: hour or minutes   Up/Down: change", "Gauche/Droite : h / min   Haut/Bas : changer"},
    {"OK: save   Back: cancel", "OK : valider   Retour : annuler"},
    /* the games' taglines */
    {"Jump to the beat", "Saute en rythme"},
    {"Hop, dodge, survive", "Saute, esquive, survis"},
    {"Mine, craft, survive", "Mine, fabrique, survis"},
    {"Drive, flip, finish", "Conduis, bascule, arrive"},
    {"Poker hands, wild jokers", "Mains de poker, jokers fous"},
    {"You, the Dealer, one shotgun", "Toi, le croupier, un fusil"},
    {"Think with portals", "Pense avec les portails"},
    {"Stack and clear lines", "Empile et efface les lignes"},
    {"Bots, puzzles, 2 players", "Bots, problèmes, 2 joueurs"},
    {"Flap through the pipes", "Vole entre les tuyaux"},
    {"Eat dots, dodge ghosts", "Mange les points, fuis les fantômes"},
    {"Eat apples, grow long", "Mange des pommes, grandis"},
    {"Four in a row wins", "Quatre alignés pour gagner"},
    {"The classic card game", "Le jeu de cartes classique"},
    {"Slide, merge, reach 2048", "Glisse, fusionne, atteins 2048"},
    {"Clear the field, flag the mines", "Déminer le terrain, pose les drapeaux"},
    {"Break every brick", "Casse chaque brique"},
    {"Backgrounds, clocks, timers", "Fonds, horloges, minuteurs"},
    /* settings: list */
    {"INSTALLED GAMES", "JEUX INSTALLÉS"},
    {" KB", " Ko"},
    {"Made with ", "Fait avec "},
    {" by Mason Chen", " par Mason Chen"},
    {"Reset", "Effacer"},
    {"Uninstall", "Supprimer"},
    {"Reset all games", "Tout effacer"},
    {"Deletes the progress of every game", "Efface la progression de tous les jeux"},
    {"Start as Matrices", "Démarrer en Matrices"},
    {"A math app first; NumPlay opens in secret", "D'abord une app de maths, NumPlay en secret"},
    {"Show a hint", "Afficher un indice"},
    {"Matrices names the secret, small and gray", "Matrices indique le secret, en petit et gris"},
    {"Secret way in", "Accès secret"},
    {"Opens NumPlay from Matrices", "Ouvre NumPlay depuis Matrices"},
    {"x,n,t key", "touche x,n,t"},
    {"var key", "touche var"},
    {"Toolbox key", "touche Boîte à outils"},
    {"Pi key", "touche Pi"},
    {"Square root key", "touche racine carrée"},
    {"Menu: Examples", "Menu : Exemples"},
    /* Matrices (the disguise): as the calculator's own app would read in French */
    {"MATRICES", "MATRICES"},
    {"Matrix", "Matrice"},
    {"Results", "Résultats"},
    {"Dimension", "Dimension"},
    {"Determinant", "Déterminant"},
    {"Inverse", "Inverse"},
    {"Not invertible", "Non inversible"},
    {"Transpose", "Transposée"},
    {"Identity matrix", "Matrice identité"},
    {"Clear", "Effacer"},
    {"Examples", "Exemples"},
    {"Toolbox", "Boîte à outils"},
    {"Matrices", "Matrices"},
    {"Language", "Langue"},
    {"English or French", "Anglais ou français"},
    {"English", "Français"},
    /* settings: dialogs */
    {"Hold OK", "Maintiens OK"},
    {"Cancel", "Annuler"},
    {"OK", "OK"},
    {"Reset every game?", "Tout effacer ?"},
    {"Reset %s?", "Effacer %s ?"},
    {"This deletes the saved progress of every game: best scores, unlocked levels and settings. You can't undo this.",
     "Cela supprime la progression enregistrée de tous les jeux : meilleurs scores, niveaux débloqués et réglages. "
     "C'est irréversible."},
    {"This deletes the saved progress of %s: best scores, unlocked levels and settings. You can't undo this.",
     "Cela supprime la progression enregistrée de %s : meilleurs scores, niveaux débloqués et réglages. "
     "C'est irréversible."},
    {"Games stay installed, and levels you made in an editor are kept.",
     "Les jeux restent installés, et les niveaux créés dans un éditeur sont conservés."},
    {"Every game was reset", "Tout est effacé"},
    {"%s was reset", "Progression effacée"},
    {"Its progress starts over.", "Elle repart de zéro."},
    {"%s was uninstalled", "%s supprimé"},
    {"%s of flash memory freed", "%s de mémoire flash libérés"},
    {"Couldn't uninstall", "Suppression impossible"},
    {"Uninstalling %s...", "Suppression en cours..."},
    {"Don't turn off your calculator.", "N'éteins pas ta calculatrice."},
    {"Uninstall %s?", "Supprimer %s ?"},
    {"This deletes %s and all of its saved progress. You can't undo this.",
     "Cela supprime %s et toute sa progression enregistrée. C'est irréversible."},
    {"To play it again, reinstall NumPlay from my.numworks.com/apps. Your other games keep their progress.",
     "Pour y rejouer, réinstalle NumPlay depuis my.numworks.com/apps. Les autres jeux gardent leur progression."},
    {"Your calculator's software doesn't allow apps to uninstall parts of themselves. Update it to Epsilon 21 or "
     "newer at my.numworks.com, then try again.",
     "Le logiciel de ta calculatrice ne permet pas aux apps de se supprimer en partie. Mets-la à jour vers "
     "Epsilon 21 ou plus récent sur my.numworks.com, puis réessaie."},
    {"The battery is too low to safely erase flash memory. Plug in your calculator, then try again.",
     "La batterie est trop faible pour effacer la mémoire flash en toute sécurité. Branche ta calculatrice, puis "
     "réessaie."},
    {"Something went wrong while erasing. To be safe, reinstall NumPlay from my.numworks.com/apps.",
     "Un problème est survenu pendant l'effacement. Par sécurité, réinstalle NumPlay depuis "
     "my.numworks.com/apps."},
};

#define NPAIRS ((int)(sizeof pairs / sizeof pairs[0]))
const char *np_t(const char *en) {
  if (!np_french || !en) return en;
  for (int i = 0; i < NPAIRS; i++)
    if (!strcmp(pairs[i].en, en)) return pairs[i].fr;
  return en;
}

/* The translated text with its first %s replaced by arg. */
char *np_fmt(char *out, unsigned n, const char *en, const char *arg) {
  const char *p = np_t(en);
  unsigned o = 0;
  for (; *p && o + 1 < n; p++) {
    if (p[0] == '%' && p[1] == 's') {
      for (const char *a = arg; *a && o + 1 < n; a++) out[o++] = *a;
      p++;
    } else out[o++] = *p;
  }
  out[o] = 0;
  return out;
}
