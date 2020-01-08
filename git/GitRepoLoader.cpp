#include "GitRepoLoader.h"

#include <GitBase.h>
#include <RevisionsCache.h>
#include <GitRequestorProcess.h>

#include <QLogger.h>

#include <QDir>

using namespace QLogger;

static const QString GIT_LOG_FORMAT = "%m%HX%P%n%cn<%ce>%n%an<%ae>%n%at%n%s%n%b";

GitRepoLoader::GitRepoLoader(QSharedPointer<GitBase> gitBase, QSharedPointer<RevisionsCache> cache, QObject *parent)
   : QObject(parent)
   , mGitBase(gitBase)
   , mRevCache(cache)
{
}

bool GitRepoLoader::loadRepository()
{
   if (mLocked)
      QLog_Warning("Git", "Git is currently loading data.");
   else
   {
      if (mGitBase->getWorkingDir().isEmpty())
         QLog_Error("Git", "No working directory set.");
      else
      {
         QLog_Info("Git", "Initializing Git...");

         mRevCache->clear();

         mLocked = true;

         if (configureRepoDirectory())
         {
            loadReferences();

            requestRevisions();

            QLog_Info("Git", "... Git init finished");

            return true;
         }
         else
            QLog_Error("Git", "The working directory is not a Git repository.");
      }
   }

   return false;
}

bool GitRepoLoader::configureRepoDirectory()
{
   const auto ret = mGitBase->run("git rev-parse --show-cdup");

   if (ret.first)
   {
      QDir d(QString("%1/%2").arg(mGitBase->getWorkingDir(), ret.second.trimmed()));
      mGitBase->setWorkingDir(d.absolutePath());

      return true;
   }

   return false;
}

void GitRepoLoader::loadReferences()
{
   const auto ret3 = mGitBase->run("git show-ref -d");

   if (ret3.first)
   {
      auto ret = mGitBase->run("git rev-parse HEAD");

      if (ret.first)
         ret.second.remove(ret.second.count() - 1, ret.second.count());

      QString prevRefSha;
      const auto curBranchSHA = ret.second;
      const auto referencesList = ret3.second.split('\n', QString::SkipEmptyParts);

      for (auto reference : referencesList)
      {
         const auto revSha = reference.left(40);
         const auto refName = reference.mid(41);

         // one Revision could have many tags
         auto cur = mRevCache->getReference(revSha);
         cur.configure(refName, curBranchSHA == revSha, prevRefSha);

         mRevCache->insertReference(revSha, std::move(cur));

         if (refName.startsWith("refs/tags/") && refName.endsWith("^{}") && !prevRefSha.isEmpty())
            mRevCache->removeReference(prevRefSha);

         prevRefSha = revSha;
      }

      // mark current head (even when detached)
      auto cur = mRevCache->getReference(curBranchSHA);
      cur.type |= CUR_BRANCH;
      mRevCache->insertReference(curBranchSHA, std::move(cur));
   }
}

void GitRepoLoader::requestRevisions()
{
   const auto baseCmd = QString("git log --date-order --no-color --log-size --parents --boundary -z --pretty=format:")
                            .append(GIT_LOG_FORMAT)
                            .append(" --all");

   const auto requestor = new GitRequestorProcess(mGitBase->getWorkingDir());
   connect(requestor, &GitRequestorProcess::procDataReady, this, &GitRepoLoader::processRevision);
   connect(this, &GitRepoLoader::cancelAllProcesses, requestor, &AGitProcess::onCancel);

   QString buf;
   requestor->run(baseCmd, buf);
}

void GitRepoLoader::processRevision(const QByteArray &ba)
{
   QByteArray auxBa = ba;
   const auto commits = ba.split('\000');
   const auto totalCommits = commits.count();
   auto count = 1;

   mRevCache->configure(totalCommits);

   emit signalLoadingStarted();

   updateWipRevision();

   for (const auto &commitInfo : commits)
   {
      CommitInfo revision(commitInfo, count++);

      if (revision.isValid())
         mRevCache->insertCommitInfo(std::move(revision));
      else
         break;
   }

   mLocked = false;

   emit signalLoadingFinished();
}

void GitRepoLoader::updateWipRevision()
{
   mRevCache->setUntrackedFilesList(getUntrackedFiles());

   const auto ret = mGitBase->run("git rev-parse --revs-only HEAD");

   if (ret.first)
   {
      const auto parentSha = ret.second.trimmed();

      const auto ret3 = mGitBase->run(QString("git diff-index %1").arg(parentSha));
      const auto diffIndex = ret3.first ? ret3.second : QString();

      const auto ret4 = mGitBase->run(QString("git diff-index --cached %1").arg(parentSha));
      const auto diffIndexCached = ret4.first ? ret4.second : QString();

      mRevCache->updateWipCommit(parentSha, diffIndex, diffIndexCached);
   }
}

QVector<QString> GitRepoLoader::getUntrackedFiles() const
{
   // add files present in working directory but not in git archive

   auto runCmd = QString("git ls-files --others");
   const auto exFile = QString(".git/info/exclude");
   const auto path = QString("%1/%2").arg(mGitBase->getWorkingDir(), exFile);

   if (QFile::exists(path))
      runCmd.append(QString(" --exclude-from=$%1$").arg(exFile));

   runCmd.append(QString(" --exclude-per-directory=$%1$").arg(".gitignore"));

   return mGitBase->run(runCmd).second.split('\n', QString::SkipEmptyParts).toVector();
}

void GitRepoLoader::cancelAll()
{
   emit cancelAllProcesses(QPrivateSignal());
}
