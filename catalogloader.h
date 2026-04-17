#ifndef CATALOGLOADER_H
#define CATALOGLOADER_H

#include "catalogtypes.h"

class CatalogLoader
{
public:
    static QString findCatalogRoot(const QString &startDir);
    static bool loadCatalog(const QString &catalogRoot, CatalogData *catalog, QString *errorMessage);
    static bool setProfileEnabled(const CatalogData &catalog,
                                  const QString &profileId,
                                  bool enabled,
                                  QString *errorMessage);
    static bool loadPatchRecipe(const CatalogData &catalog,
                                const QString &recipeId,
                                PatchRecipeData *recipe,
                                QString *errorMessage);
    static bool loadBuildRecipe(const CatalogData &catalog,
                                const QString &recipeId,
                                BuildRecipeData *recipe,
                                QString *errorMessage);
    static bool loadVerifyRecipe(const CatalogData &catalog,
                                 const QString &recipeId,
                                 VerifyRecipeData *recipe,
                                 QString *errorMessage);
};

#endif // CATALOGLOADER_H
