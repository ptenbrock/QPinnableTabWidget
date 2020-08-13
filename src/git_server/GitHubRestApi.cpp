#include "GitHubRestApi.h"
#include <Issue.h>

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QUrlQuery>

#include <QLogger.h>

using namespace QLogger;
using namespace GitServer;

GitHubRestApi::GitHubRestApi(QString repoOwner, QString repoName, const ServerAuthentication &auth, QObject *parent)
   : IRestApi(auth, parent)
{
   if (!repoOwner.endsWith("/"))
      repoOwner.append("/");

   if (!repoOwner.startsWith("/"))
      repoOwner.prepend("/");

   if (repoName.endsWith("/"))
      repoName = repoName.left(repoName.size() - 1);

   mRepoEndpoint = QString("/repos") + repoOwner + repoName;
}

void GitHubRestApi::testConnection()
{
   auto request = createRequest("/user/repos");

   const auto reply = mManager->get(request);

   connect(reply, &QNetworkReply::finished, this, [this]() {
      const auto reply = qobject_cast<QNetworkReply *>(sender());
      QString errorStr;
      const auto tmpDoc = validateData(reply, errorStr);

      if (!tmpDoc.isEmpty())
         emit connectionTested();
      else
         emit errorOccurred(errorStr);
   });
}

void GitHubRestApi::createIssue(const Issue &issue)
{
   QJsonDocument doc(issue.toJson());
   const auto data = doc.toJson(QJsonDocument::Compact);

   auto request = createRequest(mRepoEndpoint + "/issues");
   request.setRawHeader("Content-Length", QByteArray::number(data.size()));
   const auto reply = mManager->post(request, data);

   connect(reply, &QNetworkReply::finished, this, &GitHubRestApi::onIssueCreated);
}

void GitHubRestApi::updateIssue(int issueNumber, const Issue &issue)
{
   QJsonDocument doc(issue.toJson());
   const auto data = doc.toJson(QJsonDocument::Compact);

   auto request = createRequest(QString(mRepoEndpoint + "/issues/%1").arg(issueNumber));
   request.setRawHeader("Content-Length", QByteArray::number(data.size()));
   const auto reply = mManager->post(request, data);

   connect(reply, &QNetworkReply::finished, this, [this]() {
      const auto reply = qobject_cast<QNetworkReply *>(sender());
      QString errorStr;
      const auto tmpDoc = validateData(reply, errorStr);

      if (!tmpDoc.isEmpty())
         emit issueUpdated();
      else
         emit errorOccurred(errorStr);
   });
}

void GitHubRestApi::createPullRequest(const PullRequest &pullRequest)
{
   QJsonDocument doc(pullRequest.toJson());
   const auto data = doc.toJson(QJsonDocument::Compact);

   auto request = createRequest(mRepoEndpoint + "/pulls");
   request.setRawHeader("Content-Length", QByteArray::number(data.size()));

   const auto reply = mManager->post(request, data);
   connect(reply, &QNetworkReply::finished, this, &GitHubRestApi::onPullRequestCreated);
}

void GitHubRestApi::requestLabels()
{
   const auto reply = mManager->get(createRequest(mRepoEndpoint + "/labels"));

   connect(reply, &QNetworkReply::finished, this, &GitHubRestApi::onLabelsReceived);
}

void GitHubRestApi::requestMilestones()
{
   const auto reply = mManager->get(createRequest(mRepoEndpoint + "/milestones"));

   connect(reply, &QNetworkReply::finished, this, &GitHubRestApi::onMilestonesReceived);
}

void GitHubRestApi::requestIssues(int page)
{
   auto request = createRequest(mRepoEndpoint + "/issues");
   auto url = request.url();
   QUrlQuery query;

   if (page != -1)
   {
      query.addQueryItem("page", QString::number(page));
      url.setQuery(query);
   }

   query.addQueryItem("per_page", QString::number(100));
   url.setQuery(query);

   request.setUrl(url);

   const auto reply = mManager->get(request);

   connect(reply, &QNetworkReply::finished, this, &GitHubRestApi::onIssuesReceived);
}

void GitHubRestApi::requestPullRequests(int page)
{
   auto request = createRequest(mRepoEndpoint + "/pulls");
   auto url = request.url();
   QUrlQuery query;

   if (page != -1)
   {
      query.addQueryItem("page", QString::number(page));
      url.setQuery(query);
   }

   query.addQueryItem("per_page", QString::number(100));
   url.setQuery(query);

   request.setUrl(url);

   const auto reply = mManager->get(request);

   connect(reply, &QNetworkReply::finished, this, &GitHubRestApi::onPullRequestReceived);
}

void GitHubRestApi::mergePullRequest(int number, const QByteArray &data)
{
   const auto reply = mManager->put(createRequest(mRepoEndpoint + QString("/pulls/%1/merge").arg(number)), data);

   connect(reply, &QNetworkReply::finished, this, &GitHubRestApi::onPullRequestMerged);
}

void GitHubRestApi::requestComments(const Issue &issue)
{
   const auto reply = mManager->get(createRequest(mRepoEndpoint + QString("/issues/%1/comments").arg(issue.number)));

   connect(reply, &QNetworkReply::finished, this, [this, issue]() { onCommentsReceived(issue); });
}

void GitHubRestApi::requestReviews(const PullRequest &pr)
{
   const auto reply = mManager->get(createRequest(mRepoEndpoint + QString("/pulls/%1/reviews").arg(pr.number)));

   connect(reply, &QNetworkReply::finished, this, [this, pr]() { onReviewsReceived(pr); });
}

QNetworkRequest GitHubRestApi::createRequest(const QString &page) const
{
   QNetworkRequest request;
   request.setUrl(QString(mAuth.endpointUrl + page));
   request.setRawHeader("User-Agent", "GitQlient");
   request.setRawHeader("X-Custom-User-Agent", "GitQlient");
   request.setRawHeader("Content-Type", "application/json");
   request.setRawHeader("Accept", "application/vnd.github.v3+json");
   request.setRawHeader(
       QByteArray("Authorization"),
       QByteArray("Basic ")
           + QByteArray(QString(QStringLiteral("%1:%2")).arg(mAuth.userName).arg(mAuth.userPass).toLocal8Bit())
                 .toBase64());

   return request;
}

void GitHubRestApi::onLabelsReceived()
{
   const auto reply = qobject_cast<QNetworkReply *>(sender());
   QString errorStr;
   const auto tmpDoc = validateData(reply, errorStr);

   if (!tmpDoc.isEmpty())
   {
      QVector<Label> labels;
      const auto labelsArray = tmpDoc.array();

      for (auto label : labelsArray)
      {
         const auto jobObject = label.toObject();
         Label sLabel { jobObject[QStringLiteral("id")].toInt(),
                        jobObject[QStringLiteral("node_id")].toString(),
                        jobObject[QStringLiteral("url")].toString(),
                        jobObject[QStringLiteral("name")].toString(),
                        jobObject[QStringLiteral("description")].toString(),
                        jobObject[QStringLiteral("color")].toString(),
                        jobObject[QStringLiteral("default")].toBool() };

         labels.append(std::move(sLabel));
      }

      emit labelsReceived(labels);
   }
   else
      emit errorOccurred(errorStr);
}

void GitHubRestApi::onMilestonesReceived()
{
   const auto reply = qobject_cast<QNetworkReply *>(sender());
   QString errorStr;
   const auto tmpDoc = validateData(reply, errorStr);

   if (!tmpDoc.isEmpty())
   {
      QVector<Milestone> milestones;
      const auto labelsArray = tmpDoc.array();

      for (auto label : labelsArray)
      {
         const auto jobObject = label.toObject();
         Milestone sMilestone { jobObject[QStringLiteral("id")].toInt(),
                                jobObject[QStringLiteral("number")].toInt(),
                                jobObject[QStringLiteral("node_id")].toString(),
                                jobObject[QStringLiteral("title")].toString(),
                                jobObject[QStringLiteral("description")].toString(),
                                jobObject[QStringLiteral("state")].toString() == "open" };

         milestones.append(std::move(sMilestone));
      }

      emit milestonesReceived(milestones);
   }
   else
      emit errorOccurred(errorStr);
}

void GitHubRestApi::onIssueCreated()
{
   const auto reply = qobject_cast<QNetworkReply *>(sender());
   QString errorStr;
   const auto tmpDoc = validateData(reply, errorStr);

   if (!tmpDoc.isEmpty())
   {
      const auto issueData = tmpDoc.object();
      Issue issue;
      issue.number = issueData["number"].toInt();
      issue.title = issueData["title"].toString();
      issue.body = issueData["body"].toString().toUtf8();
      issue.url = issueData["html_url"].toString();
      issue.creation = issueData["created_at"].toVariant().toDateTime();
      issue.commentsCount = issueData["comments"].toInt();

      issue.creator
          = { issueData["user"].toObject()["id"].toInt(), issueData["user"].toObject()["login"].toString(),
              issueData["user"].toObject()["avatar_url"].toString(),
              issueData["user"].toObject()["html_url"].toString(), issueData["user"].toObject()["type"].toString() };

      const auto labels = issueData["labels"].toArray();

      for (auto label : labels)
      {
         issue.labels.append({ label["id"].toInt(), label["node_id"].toString(), label["url"].toString(),
                               label["name"].toString(), label["description"].toString(), label["color"].toString(),
                               label["default"].toBool() });
      }

      const auto assignees = issueData["assignees"].toArray();

      for (auto assignee : assignees)
      {
         GitServer::User sAssignee;
         sAssignee.id = assignee["id"].toInt();
         sAssignee.url = assignee["html_url"].toString();
         sAssignee.name = assignee["login"].toString();
         sAssignee.avatar = assignee["avatar_url"].toString();

         issue.assignees.append(sAssignee);
      }

      Milestone sMilestone { issueData["milestone"].toObject()[QStringLiteral("id")].toInt(),
                             issueData["milestone"].toObject()[QStringLiteral("number")].toInt(),
                             issueData["milestone"].toObject()[QStringLiteral("node_id")].toString(),
                             issueData["milestone"].toObject()[QStringLiteral("title")].toString(),
                             issueData["milestone"].toObject()[QStringLiteral("description")].toString(),
                             issueData["milestone"].toObject()[QStringLiteral("state")].toString() == "open" };

      issue.milestone = sMilestone;

      emit issueCreated(issue);
   }
   else
      emit errorOccurred(errorStr);
}

void GitHubRestApi::onPullRequestCreated()
{
   const auto reply = qobject_cast<QNetworkReply *>(sender());
   QString errorStr;
   const auto tmpDoc = validateData(reply, errorStr);

   if (!tmpDoc.isEmpty())
   {

      const auto issueData = tmpDoc.object();
      const auto number = issueData["number"].toInt();
      PullRequest pr;
      pr.number = issueData["number"].toInt();
      pr.title = issueData["title"].toString();
      pr.body = issueData["body"].toString().toUtf8();
      pr.url = issueData["html_url"].toString();
      pr.head = issueData["head"].toObject()["ref"].toString();
      pr.state.sha = issueData["head"].toObject()["sha"].toString();
      pr.base = issueData["base"].toObject()["ref"].toString();
      pr.isOpen = issueData["state"].toString() == "open";
      pr.draft = issueData["draft"].toBool();
      pr.creation = issueData["created_at"].toVariant().toDateTime();

      pr.creator
          = { issueData["user"].toObject()["id"].toInt(), issueData["user"].toObject()["login"].toString(),
              issueData["user"].toObject()["avatar_url"].toString(),
              issueData["user"].toObject()["html_url"].toString(), issueData["user"].toObject()["type"].toString() };

      const auto labels = issueData["labels"].toArray();

      for (auto label : labels)
      {
         pr.labels.append({ label["id"].toInt(), label["node_id"].toString(), label["url"].toString(),
                            label["name"].toString(), label["description"].toString(), label["color"].toString(),
                            label["default"].toBool() });
      }

      const auto assignees = issueData["assignees"].toArray();

      for (auto assignee : assignees)
      {
         GitServer::User sAssignee;
         sAssignee.id = assignee["id"].toInt();
         sAssignee.url = assignee["html_url"].toString();
         sAssignee.name = assignee["login"].toString();
         sAssignee.avatar = assignee["avatar_url"].toString();

         pr.assignees.append(sAssignee);
      }

      Milestone sMilestone { issueData["milestone"].toObject()[QStringLiteral("id")].toInt(),
                             issueData["milestone"].toObject()[QStringLiteral("number")].toInt(),
                             issueData["milestone"].toObject()[QStringLiteral("node_id")].toString(),
                             issueData["milestone"].toObject()[QStringLiteral("title")].toString(),
                             issueData["milestone"].toObject()[QStringLiteral("description")].toString(),
                             issueData["milestone"].toObject()[QStringLiteral("state")].toString() == "open" };

      pr.milestone = sMilestone;

      mPullRequests.insert(number, std::move(pr));

      /*
         QTimer::singleShot(200, [this, number]() {
            const auto reply = mManager->get(createRequest(mRepoEndpoint + QString("/pulls/%1").arg(number)));
            connect(reply, &QNetworkReply::finished, this, &GitHubRestApi::onPullRequestDetailesReceived);
         });
         */
      QTimer::singleShot(200, [this, pr]() {
         auto request = createRequest(mRepoEndpoint + QString("/commits/%1/status").arg(pr.state.sha));
         const auto reply = mManager->get(request);
         connect(reply, &QNetworkReply::finished, this, [this, pr] { onPullRequestStatusReceived(pr); });
      });

      emit pullRequestCreated(pr);
   }
   else
      emit errorOccurred(errorStr);
}

void GitHubRestApi::onPullRequestMerged()
{
   const auto reply = qobject_cast<QNetworkReply *>(sender());
   QString errorStr;
   const auto tmpDoc = validateData(reply, errorStr);

   if (!tmpDoc.isEmpty())
      emit pullRequestMerged();
   else
      emit errorOccurred(errorStr);
}

void GitHubRestApi::onPullRequestReceived()
{
   mPullRequests.clear();

   const auto reply = qobject_cast<QNetworkReply *>(sender());

   if (const auto pagination = QString::fromUtf8(reply->rawHeader("Link")); !pagination.isEmpty())
   {
      QStringList pages = pagination.split(",");
      auto current = 0;
      auto next = 0;
      auto total = 0;

      for (auto page : pages)
      {
         const auto values = page.trimmed().remove("<").remove(">").split(";");

         if (values.last().contains("next"))
         {
            next = values.first().split("page=").last().toInt();
            current = next - 1;
         }
         else if (values.last().contains("last"))
            total = values.first().split("page=").last().toInt();
      }

      emit paginationPresent(current, next, total);
   }
   else
      emit paginationPresent(0, 0, 0);

   const auto url = reply->url();
   QString errorStr;
   const auto tmpDoc = validateData(reply, errorStr);

   if (!tmpDoc.isEmpty())
   {
      const auto issuesArray = tmpDoc.array();
      mPullRequestsRequested = issuesArray.count();

      for (const auto &issueData : issuesArray)
      {
         const auto number = issueData["number"].toInt();
         PullRequest pr;
         pr.number = issueData["number"].toInt();
         pr.title = issueData["title"].toString();
         pr.body = issueData["body"].toString().toUtf8();
         pr.url = issueData["html_url"].toString();
         pr.head = issueData["head"].toObject()["ref"].toString();
         pr.state.sha = issueData["head"].toObject()["sha"].toString();
         pr.base = issueData["base"].toObject()["ref"].toString();
         pr.isOpen = issueData["state"].toString() == "open";
         pr.draft = issueData["draft"].toBool();
         pr.creation = issueData["created_at"].toVariant().toDateTime();

         pr.creator
             = { issueData["user"].toObject()["id"].toInt(), issueData["user"].toObject()["login"].toString(),
                 issueData["user"].toObject()["avatar_url"].toString(),
                 issueData["user"].toObject()["html_url"].toString(), issueData["user"].toObject()["type"].toString() };

         const auto labels = issueData["labels"].toArray();

         for (auto label : labels)
         {
            pr.labels.append({ label["id"].toInt(), label["node_id"].toString(), label["url"].toString(),
                               label["name"].toString(), label["description"].toString(), label["color"].toString(),
                               label["default"].toBool() });
         }

         const auto assignees = issueData["assignees"].toArray();

         for (auto assignee : assignees)
         {
            GitServer::User sAssignee;
            sAssignee.id = assignee["id"].toInt();
            sAssignee.url = assignee["html_url"].toString();
            sAssignee.name = assignee["login"].toString();
            sAssignee.avatar = assignee["avatar_url"].toString();

            pr.assignees.append(sAssignee);
         }

         Milestone sMilestone { issueData["milestone"].toObject()[QStringLiteral("id")].toInt(),
                                issueData["milestone"].toObject()[QStringLiteral("number")].toInt(),
                                issueData["milestone"].toObject()[QStringLiteral("node_id")].toString(),
                                issueData["milestone"].toObject()[QStringLiteral("title")].toString(),
                                issueData["milestone"].toObject()[QStringLiteral("description")].toString(),
                                issueData["milestone"].toObject()[QStringLiteral("state")].toString() == "open" };

         pr.milestone = sMilestone;

         mPullRequests.insert(number, std::move(pr));

         /*
         QTimer::singleShot(200, [this, number]() {
            const auto reply = mManager->get(createRequest(mRepoEndpoint + QString("/pulls/%1").arg(number)));
            connect(reply, &QNetworkReply::finished, this, &GitHubRestApi::onPullRequestDetailesReceived);
         });
         */
         QTimer::singleShot(200, [this, pr]() {
            auto request = createRequest(mRepoEndpoint + QString("/commits/%1/status").arg(pr.state.sha));
            const auto reply = mManager->get(request);
            connect(reply, &QNetworkReply::finished, this, [this, pr] { onPullRequestStatusReceived(pr); });
         });
      }

      auto prs = mPullRequests.values().toVector();
      std::sort(prs.begin(), prs.end(),
                [](const PullRequest &p1, const PullRequest &p2) { return p1.creation > p2.creation; });
      emit pullRequestsReceived(prs);
   }
}

void GitHubRestApi::onPullRequestStatusReceived(PullRequest pr)
{
   const auto reply = qobject_cast<QNetworkReply *>(sender());
   QString errorStr;
   const auto tmpDoc = validateData(reply, errorStr);

   if (!tmpDoc.isEmpty())
   {
      const auto obj = tmpDoc.object();

      pr.state.state = obj["state"].toString();

      pr.state.eState = pr.state.state == "success"
          ? PullRequest::HeadState::State::Success
          : pr.state.state == "failure" ? PullRequest::HeadState::State::Failure
                                        : PullRequest::HeadState::State::Pending;

      const auto statuses = obj["statuses"].toArray();

      for (auto status : statuses)
      {
         auto statusStr = status["state"].toString();

         if (statusStr == "ok")
            statusStr = "success";
         else if (statusStr == "error")
            statusStr = "failure";

         PullRequest::HeadState::Check check { status["description"].toString(), statusStr,
                                               status["target_url"].toString(), status["context"].toString() };

         pr.state.checks.append(std::move(check));
      }

      emit pullRequestsStateReceived(pr);
   }
   else
      emit errorOccurred(errorStr);
}

void GitHubRestApi::onIssuesReceived()
{
   const auto reply = qobject_cast<QNetworkReply *>(sender());

   if (const auto pagination = QString::fromUtf8(reply->rawHeader("Link")); !pagination.isEmpty())
   {
      QStringList pages = pagination.split(",");
      auto current = 0;
      auto next = 0;
      auto total = 0;

      for (auto page : pages)
      {
         const auto values = page.trimmed().remove("<").remove(">").split(";");

         if (values.last().contains("next"))
         {
            next = values.first().split("page=").last().toInt();
            current = next - 1;
         }
         else if (values.last().contains("last"))
            total = values.first().split("page=").last().toInt();
      }

      emit paginationPresent(current, next, total);
   }
   else
      emit paginationPresent(0, 0, 0);

   const auto url = reply->url();
   QString errorStr;
   const auto tmpDoc = validateData(reply, errorStr);

   if (!tmpDoc.isEmpty())
   {
      QVector<Issue> issues;
      const auto issuesArray = tmpDoc.array();

      for (const auto &issueData : issuesArray)
      {
         if (const auto issueObj = issueData.toObject(); !issueObj.contains("pull_request"))
         {
            Issue issue;
            issue.number = issueData["number"].toInt();
            issue.title = issueData["title"].toString();
            issue.body = issueData["body"].toString().toUtf8();
            issue.url = issueData["html_url"].toString();
            issue.creation = issueData["created_at"].toVariant().toDateTime();
            issue.commentsCount = issueData["comments"].toInt();

            issue.creator
                = { issueData["user"].toObject()["id"].toInt(), issueData["user"].toObject()["login"].toString(),
                    issueData["user"].toObject()["avatar_url"].toString(),
                    issueData["user"].toObject()["html_url"].toString(),
                    issueData["user"].toObject()["type"].toString() };

            const auto labels = issueData["labels"].toArray();

            for (auto label : labels)
            {
               issue.labels.append({ label["id"].toInt(), label["node_id"].toString(), label["url"].toString(),
                                     label["name"].toString(), label["description"].toString(),
                                     label["color"].toString(), label["default"].toBool() });
            }

            const auto assignees = issueData["assignees"].toArray();

            for (auto assignee : assignees)
            {
               GitServer::User sAssignee;
               sAssignee.id = assignee["id"].toInt();
               sAssignee.url = assignee["html_url"].toString();
               sAssignee.name = assignee["login"].toString();
               sAssignee.avatar = assignee["avatar_url"].toString();

               issue.assignees.append(sAssignee);
            }

            Milestone sMilestone { issueData["milestone"].toObject()[QStringLiteral("id")].toInt(),
                                   issueData["milestone"].toObject()[QStringLiteral("number")].toInt(),
                                   issueData["milestone"].toObject()[QStringLiteral("node_id")].toString(),
                                   issueData["milestone"].toObject()[QStringLiteral("title")].toString(),
                                   issueData["milestone"].toObject()[QStringLiteral("description")].toString(),
                                   issueData["milestone"].toObject()[QStringLiteral("state")].toString() == "open" };

            issue.milestone = sMilestone;

            issues.append(std::move(issue));
         }
      }

      if (!issues.isEmpty())
         emit issuesReceived(issues);

      for (auto &issue : issues)
         QTimer::singleShot(200, this, [this, issue]() { requestComments(issue); });
   }
}

void GitHubRestApi::onCommentsReceived(Issue issue)
{
   const auto reply = qobject_cast<QNetworkReply *>(sender());
   const auto url = reply->url();
   QString errorStr;
   const auto tmpDoc = validateData(reply, errorStr);

   if (!tmpDoc.isEmpty())
   {
      QVector<Comment> comments;
      const auto commentsArray = tmpDoc.array();

      for (const auto &commentData : commentsArray)
      {
         Comment c;
         c.id = commentData["id"].toInt();
         c.body = commentData["body"].toString();
         c.creation = commentData["created_at"].toVariant().toDateTime();
         c.association = commentData["author_association"].toString();

         GitServer::User sAssignee;
         sAssignee.id = commentData["user"].toObject()["id"].toInt();
         sAssignee.url = commentData["user"].toObject()["html_url"].toString();
         sAssignee.name = commentData["user"].toObject()["login"].toString();
         sAssignee.avatar = commentData["user"].toObject()["avatar_url"].toString();
         sAssignee.type = commentData["user"].toObject()["type"].toString();

         c.creator = std::move(sAssignee);
         comments.append(std::move(c));
      }

      issue.comments = comments;

      emit commentsReceived(issue);
   }
}

void GitHubRestApi::onPullRequestDetailesReceived()
{
   const auto reply = qobject_cast<QNetworkReply *>(sender());
   QString errorStr;
   const auto tmpDoc = validateData(reply, errorStr);

   if (!tmpDoc.isEmpty())
   {
      const auto prInfo = tmpDoc.object();

      auto &issue = mPullRequests[prInfo["number"].toInt()];
      issue.commentsCount = prInfo["comments"].toInt();
      issue.reviewCommentsCount = prInfo["review_comments"].toInt();
      issue.commits = prInfo["commits"].toInt();
      issue.additions = prInfo["aditions"].toInt();
      issue.deletions = prInfo["deletions"].toInt();
      issue.changedFiles = prInfo["changed_files"].toInt();
      issue.merged = prInfo["merged"].toBool();
      issue.mergeable = prInfo["mergeable"].toBool();
      issue.rebaseable = prInfo["rebaseable"].toBool();
      issue.mergeableState = prInfo["mergeable_state"].toString();

      --mPullRequestsRequested;

      if (mPullRequestsRequested == 0)
      {
         auto prs = mPullRequests.values().toVector();
         std::sort(prs.begin(), prs.end(),
                   [](const PullRequest &p1, const PullRequest &p2) { return p1.creation > p2.creation; });
         emit pullRequestsReceived(prs);
      }
   }
}

void GitHubRestApi::onReviewsReceived(PullRequest pr)
{
   const auto reply = qobject_cast<QNetworkReply *>(sender());
   const auto url = reply->url();
   QString errorStr;
   const auto tmpDoc = validateData(reply, errorStr);

   if (!tmpDoc.isEmpty())
   {
      QMap<int, Review> reviews;
      const auto commentsArray = tmpDoc.array();

      for (const auto &commentData : commentsArray)
      {
         auto id = commentData["id"].toInt();

         Review r;
         r.id = id;
         r.body = commentData["body"].toString();
         r.creation = commentData["submitted_at"].toVariant().toDateTime();
         r.state = commentData["state"].toString();
         r.association = commentData["author_association"].toString();

         GitServer::User sAssignee;
         sAssignee.id = commentData["user"].toObject()["id"].toInt();
         sAssignee.url = commentData["user"].toObject()["html_url"].toString();
         sAssignee.name = commentData["user"].toObject()["login"].toString();
         sAssignee.avatar = commentData["user"].toObject()["avatar_url"].toString();
         sAssignee.type = commentData["user"].toObject()["type"].toString();

         r.creator = std::move(sAssignee);
         reviews.insert(id, std::move(r));
      }

      pr.reviews = reviews;

      QTimer::singleShot(200, this, [this, pr]() { requestReviewComments(pr); });
   }
}

void GitHubRestApi::requestReviewComments(const PullRequest &pr)
{
   const auto reply = mManager->get(createRequest(mRepoEndpoint + QString("/pulls/%1/comments").arg(pr.number)));

   connect(reply, &QNetworkReply::finished, this, [this, pr]() { onReviewCommentsReceived(pr); });
}

void GitHubRestApi::onReviewCommentsReceived(PullRequest pr)
{
   const auto reply = qobject_cast<QNetworkReply *>(sender());
   const auto url = reply->url();
   QString errorStr;
   const auto tmpDoc = validateData(reply, errorStr);

   if (!tmpDoc.isEmpty())
   {
      QVector<CodeReview> comments;
      const auto commentsArray = tmpDoc.array();

      for (const auto &commentData : commentsArray)
      {
         CodeReview c;
         c.id = commentData["id"].toInt();
         c.body = commentData["body"].toString();
         c.creation = commentData["created_at"].toVariant().toDateTime();
         c.association = commentData["author_association"].toString();
         c.diff.diff = commentData["diff_hunk"].toString();
         c.diff.file = commentData["path"].toString();
         c.diff.line = commentData["line"].toInt();
         c.diff.originalLine = commentData["original_line"].toInt();
         c.reviewId = commentData["pull_request_review_id"].toInt();
         c.replyToId = commentData["in_reply_to_id"].toInt();

         GitServer::User sAssignee;
         sAssignee.id = commentData["user"].toObject()["id"].toInt();
         sAssignee.url = commentData["user"].toObject()["html_url"].toString();
         sAssignee.name = commentData["user"].toObject()["login"].toString();
         sAssignee.avatar = commentData["user"].toObject()["avatar_url"].toString();
         sAssignee.type = commentData["user"].toObject()["type"].toString();

         c.creator = std::move(sAssignee);
         comments.append(std::move(c));
      }

      pr.reviewComment = comments;

      emit reviewsReceived(pr);
   }
}
