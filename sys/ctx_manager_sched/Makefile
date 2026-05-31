MODULE = ctx_manager_sched

# -----------------------------------------------------------------------
# Pourquoi ces suppressions sont nécessaires
# -----------------------------------------------------------------------
#
# 1. -Wno-unused-function
#    ctx_save_asm(), ctx_restore_asm(), _ctx_first_launch_asm() sont des
#    fonctions NAKED appelées UNIQUEMENT via `bl <symbol>` dans des blocs
#    __asm__ inline.  GCC ne parse pas l'intérieur des blocs asm pour
#    trouver des références à des symboles → il considère ces fonctions
#    statiques comme "définies mais jamais utilisées" et émet une erreur
#    avec -Werror=unused-function.  C'est un faux positif structurel.
#
# 2. -Wno-unused-parameter
#    Les wrappers ctx_save() / ctx_restore() / _ctx_first_launch()
#    reçoivent leurs arguments en paramètres C mais les passent au code
#    ASM via des contraintes "r" dans __asm__ volatile.  GCC ne reconnaît
#    pas l'usage d'un paramètre C quand il n'apparaît que dans une
#    contrainte asm → faux positif unused-parameter.
#
# On utilise CFLAGS au niveau module (et non par fichier) car la syntaxe
# <fichier>_CFLAGS n'est pas garantie dans toutes les versions de RIOT.
#
CFLAGS += -Wno-unused-function
CFLAGS += -Wno-unused-parameter
CFLAGS += -Wno-unused-variable

include $(RIOTBASE)/Makefile.base