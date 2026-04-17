#ifndef CATALOGTYPES_H
#define CATALOGTYPES_H

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

enum class SourceKind
{
    Unknown,
    Directory,
    ZipArchive
};

inline QString sourceKindToString(SourceKind kind)
{
    switch (kind) {
    case SourceKind::Directory:
        return QStringLiteral("directory");
    case SourceKind::ZipArchive:
        return QStringLiteral("zip");
    case SourceKind::Unknown:
    default:
        return QStringLiteral("unknown");
    }
}

struct DetectionHint
{
    QString type;
    QString path;
    QString regex;
    QString expectedText;
    QString description;
};

struct ApplicabilityCheck
{
    QString type;
    QString path;
    QString regex;
    int expectedCount = -1;
    bool blocking = false;
    QString description;
};

struct PatchOperation
{
    QString type;
    QString targetPath;
    QString sourcePath;
    QString matchRegex;
    QString insertMode;
    int expectedMatchCount = -1;
    QString description;
};

struct VersionProfile
{
    bool enabled = true;
    QString filePath;
    QString id;
    QString displayName;
    QString family;
    QString patchRecipeId;
    QString status;
    int priority = 0;
    QStringList tags;
    QStringList archiveRoots;
    QStringList folderNamePatterns;
    QVector<DetectionHint> detectionHints;
    QStringList buildRecipeIds;
    QStringList verifyRecipeIds;
    QStringList notes;
};

struct RecipeSummary
{
    bool enabled = true;
    QString id;
    QString displayName;
    QString status;
    QString schemaType;
    QString filePath;
};

struct PatchRecipeData
{
    bool enabled = true;
    QString filePath;
    QString id;
    QString displayName;
    QString description;
    QString status;
    QStringList supportedFamilies;
    QString patchPackageId;
    QString patchPackageRootHint;
    QStringList patchPackageRootHints;
    QStringList patchPackageRequiredFiles;
    QStringList requiredFiles;
    QStringList optionalFiles;
    QVector<ApplicabilityCheck> applicabilityChecks;
    QVector<PatchOperation> operations;
    QString backupMode;
    QString backupSuffix;
    QVector<ApplicabilityCheck> idempotencyRules;
    QVector<ApplicabilityCheck> postApplyChecks;
    QStringList notes;
};

struct BuildLogParsingRule
{
    QString type;
    QString regex;
    QString description;
};

struct BuildRecipeData
{
    bool enabled = true;
    QString filePath;
    QString id;
    QString displayName;
    QString description;
    QString status;
    QString environmentOs;
    QString environmentShell;
    QString environmentHostTriplet;
    QString environmentTargetTriplet;
    QString workingDirectoryTemplate;
    QString scriptRef;
    QStringList cleanCommand;
    QStringList configureCommand;
    QStringList buildCommand;
    QStringList requiredTools;
    QStringList expectedOutputs;
    QVector<BuildLogParsingRule> logParsingRules;
    int buildTimeoutSeconds = 0;
    QString artifactDropDir;
    QStringList platformNotes;
    QStringList notes;
};

struct VerifyCheck
{
    QString type;
    QString path;
    QStringList command;
    QString regex;
    QString scriptRef;
    QString description;
};

struct VerifyRecipeData
{
    bool enabled = true;
    QString filePath;
    QString id;
    QString displayName;
    QString description;
    QString status;
    QVector<VerifyCheck> checks;
    QStringList requiredInputs;
    bool allChecksMustPass = true;
    QStringList expectedSummaryFields;
    QStringList resultArtifacts;
    QStringList notes;
};

struct CatalogData
{
    QString rootDir;
    QString catalogPath;
    QVector<VersionProfile> profiles;
    QHash<QString, RecipeSummary> patchRecipes;
    QHash<QString, RecipeSummary> buildRecipes;
    QHash<QString, RecipeSummary> verifyRecipes;

    bool isEmpty() const
    {
        return profiles.isEmpty();
    }
};

struct SourceInspection
{
    SourceKind kind = SourceKind::Unknown;
    QString inputPath;
    QString rootName;
    QString productName;
    QString gccBaseVersion;
    QString releaseLevel;
    QString inferredRef;
    bool hasLdLayout = false;
    QStringList evidence;
    QStringList warnings;
};

struct ProfileCandidate
{
    QString profileId;
    QString displayName;
    QString status;
    bool matched = false;
    int score = 0;
    int adjustedScore = 0;
    QString confidence;
    QStringList evidence;
    QStringList warnings;
};

struct ProfileMatchResult
{
    bool matched = false;
    QString bestCandidateProfileId;
    QString bestCandidateDisplayName;
    QString matchedProfileId;
    QString matchedDisplayName;
    QString confidence;
    int score = 0;
    QStringList evidence;
    QStringList warnings;
    QVector<ProfileCandidate> candidates;
};

struct SourcePreparationResult
{
    bool ok = false;
    SourceKind kind = SourceKind::Unknown;
    QString inputPath;
    QString workingRootPath;
    QString extractedWorkDir;
    QString rootName;
    QStringList messages;
    QString errorMessage;
};

struct ValidationCheckResult
{
    bool passed = false;
    bool blocking = false;
    QString description;
    QString detail;
};

struct PatchValidationDetails
{
    bool ok = false;
    bool applicable = false;
    bool alreadyPatched = false;
    QString supportLevel;
    QString recipeId;
    QString recipeDisplayName;
    QString sourceRootPath;
    QString patchPackageRoot;
    QString errorMessage;
    QVector<ValidationCheckResult> checks;
    QVector<ValidationCheckResult> idempotencyChecks;
    QStringList warnings;
};

#endif // CATALOGTYPES_H
