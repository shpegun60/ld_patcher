#include "detector.h"

#include "sourcepackage.h"

#include <algorithm>
#include <QRegularExpression>
#include <limits>

namespace {

QString confidenceFromScore(int score)
{
    if (score >= 60) {
        return QStringLiteral("high");
    }
    if (score >= 35) {
        return QStringLiteral("medium");
    }
    if (score > 0) {
        return QStringLiteral("low");
    }
    return QStringLiteral("unknown");
}

bool wildcardMatches(const QString &pattern, const QString &value)
{
    const QString regexText = QRegularExpression::wildcardToRegularExpression(pattern);
    const QRegularExpression regex(regexText, QRegularExpression::CaseInsensitiveOption);
    return regex.match(value).hasMatch();
}

struct CandidateScore
{
    int score = 0;
    int adjustedScore = 0;
    bool hasPrimaryRouteEvidence = false;
    QStringList evidence;
    QStringList warnings;
};

CandidateScore evaluateProfile(const SourcePackage &source, const VersionProfile &profile)
{
    CandidateScore result;

    const QString rootName = source.rootName();
    const SourceInspection inspection = source.inspectBasic(nullptr);

    for (const QString &candidate : profile.archiveRoots) {
        if (candidate.compare(rootName, Qt::CaseInsensitive) == 0) {
            result.score += 25;
            result.hasPrimaryRouteEvidence = true;
            result.evidence.append(QStringLiteral("Archive root matched: %1").arg(candidate));
        }
    }

    for (const QString &pattern : profile.folderNamePatterns) {
        if (wildcardMatches(pattern, rootName)) {
            result.score += 18;
            result.hasPrimaryRouteEvidence = true;
            result.evidence.append(QStringLiteral("Folder pattern matched: %1").arg(pattern));
        }
    }

    for (const QString &tag : profile.tags) {
        if (!inspection.inferredRef.isEmpty()
            && inspection.inferredRef.compare(tag, Qt::CaseInsensitive) == 0) {
            result.score += 22;
            result.hasPrimaryRouteEvidence = true;
            result.evidence.append(QStringLiteral("Inferred ref matched tag: %1").arg(tag));
        } else if (rootName.contains(tag, Qt::CaseInsensitive)) {
            result.score += 12;
            result.hasPrimaryRouteEvidence = true;
            result.evidence.append(QStringLiteral("Root name contains tag: %1").arg(tag));
        }
    }

    for (const DetectionHint &hint : profile.detectionHints) {
        if (hint.type == QStringLiteral("path_exists")) {
            if (source.existsRelative(hint.path)) {
                result.score += 8;
                result.evidence.append(hint.description);
            } else {
                result.warnings.append(QStringLiteral("Missing expected path: %1").arg(hint.path));
            }
        } else if (hint.type == QStringLiteral("file_regex")) {
            QString readError;
            const QString text = source.readTextRelative(hint.path, &readError);
            if (text.isNull()) {
                result.warnings.append(readError.isEmpty()
                                           ? QStringLiteral("Could not read %1").arg(hint.path)
                                           : readError);
                continue;
            }

            const QRegularExpression regex(hint.regex, QRegularExpression::MultilineOption);
            if (!regex.isValid()) {
                result.warnings.append(QStringLiteral("Invalid regex in detection hint for %1: %2")
                                           .arg(hint.path, regex.errorString()));
                continue;
            }
            if (regex.match(text).hasMatch()) {
                result.score += 10;
                result.evidence.append(hint.description);
            } else {
                result.warnings.append(QStringLiteral("Regex did not match in %1").arg(hint.path));
            }
        }
    }

    return result;
}

} // namespace

ProfileMatchResult Detector::matchBestProfile(const SourcePackage &source,
                                              const CatalogData &catalog,
                                              QString *errorMessage)
{
    if (!source.isOpen()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Source package is not open");
        }
        return {};
    }

    if (catalog.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Catalog is empty");
        }
        return {};
    }

    ProfileMatchResult best;
    int bestAdjustedScore = std::numeric_limits<int>::min();
    QVector<ProfileCandidate> rankedCandidates;
    for (const VersionProfile &profile : catalog.profiles) {
        if (!profile.enabled) {
            continue;
        }
        CandidateScore candidate = evaluateProfile(source, profile);
        candidate.adjustedScore = candidate.score + profile.priority;

        if (candidate.score > 0 || candidate.hasPrimaryRouteEvidence) {
            ProfileCandidate item;
            item.profileId = profile.id;
            item.displayName = profile.displayName;
            item.status = profile.status;
            item.matched = candidate.score > 0 && candidate.hasPrimaryRouteEvidence;
            item.score = candidate.score;
            item.adjustedScore = candidate.adjustedScore;
            item.confidence = confidenceFromScore(candidate.score);
            item.evidence = candidate.evidence;
            item.warnings = candidate.warnings;
            rankedCandidates.append(item);
        }

        if (candidate.adjustedScore > bestAdjustedScore) {
            bestAdjustedScore = candidate.adjustedScore;
            best.matched = false;
            best.bestCandidateProfileId = profile.id;
            best.bestCandidateDisplayName = profile.displayName;
            best.matchedProfileId.clear();
            best.matchedDisplayName.clear();
            best.score = candidate.score;
            best.evidence = candidate.evidence;
            best.warnings = candidate.warnings;
        }
    }

    std::sort(rankedCandidates.begin(),
              rankedCandidates.end(),
              [](const ProfileCandidate &left, const ProfileCandidate &right) {
                  if (left.matched != right.matched) {
                      return left.matched && !right.matched;
                  }
                  if (left.adjustedScore != right.adjustedScore) {
                      return left.adjustedScore > right.adjustedScore;
                  }
                  if (left.score != right.score) {
                      return left.score > right.score;
                  }
                  return left.displayName.compare(right.displayName, Qt::CaseInsensitive) < 0;
              });
    best.candidates = rankedCandidates;

    for (const ProfileCandidate &candidate : std::as_const(best.candidates)) {
        if (candidate.matched) {
            best.matched = true;
            best.matchedProfileId = candidate.profileId;
            best.matchedDisplayName = candidate.displayName;
            break;
        }
    }

    if (!best.bestCandidateProfileId.isEmpty()) {
        best.confidence = confidenceFromScore(best.score);
    }

    if (!best.matched) {
        best.warnings.prepend(QStringLiteral("No profile matched with a positive score"));
    }

    return best;
}
