#include <nlohmann/json.hpp>

#include "server/http/routes.h"

// AWS SDK endpoints that the client calls directly. Replies keep the SDK quiet; no data
// leaves the computer.

namespace soulwarden::backend::server::http {
namespace {

using nlohmann::json;

/** The SDK measures region latency with this route. */
service::HttpResponse ping(const RouteContext& context) {
    service::HttpResponse response;
    response.contentType = "text/plain";
    std::string host = context.request.host;
    host = host.substr(0, host.find(':'));
    response.body = "healthy: " + host + " ";
    return response;
}

/** S3 remote configuration layers. The local setup has no remote layer but the one below. */
service::HttpResponse s3_missing(const RouteContext& context) {
    service::HttpResponse response;
    response.status = 404;
    response.contentType = "application/xml";
    response.body = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Error><Code>NoSuchKey</Code>"
                    "<Message>The specified key does not exist.</Message><Key>"
                    + context.request.path.substr(1)
                    + "</Key><RequestId>SOULWARDENLOCAL</RequestId><HostId>soulwarden-local</HostId></Error>";
    return response;
}

/**
 * The remote configuration document of the gameplay settings of the product (protocol notes
 * 2.3). The client asks for it one time at its start, before the main menu. The document
 * turns the melee contact report of the client on: the client then puts each melee contact
 * that it finds into the third list of its input report (type 1589), and the world checks
 * the contact (world/combat.h). Only the exact request gets it.
 */
service::HttpResponse s3_gameplay_configuration(const RouteContext& context) {
    std::string host = context.request.host;
    host = host.substr(0, host.find(':'));
    if (host != kRemoteConfigurationHost) {
        return s3_missing(context);
    }
    service::HttpResponse response;
    response.contentType = "application/json";
    response.body = kMeleeReportDocument;
    return response;
}

/** STS AssumeRole for the telemetry role. The credentials are local placeholders. */
service::HttpResponse sts(const RouteContext&) {
    service::HttpResponse response;
    response.contentType = "text/xml";
    response.body =
        "<AssumeRoleResponse xmlns=\"https://sts.amazonaws.com/doc/2011-06-15/\"><AssumeRoleResult>"
        "<AssumedRoleUser><AssumedRoleId>AROASOULWARDENLOCAL:soulwarden</AssumedRoleId>"
        "<Arn>arn:aws:sts::000000000000:assumed-role/soulwarden-local/soulwarden</Arn></AssumedRoleUser>"
        "<Credentials><AccessKeyId>ASIASOULWARDENLOCAL000001</AccessKeyId>"
        "<SecretAccessKey>soulwarden/local/secret/access/key/0000000002</SecretAccessKey>"
        "<SessionToken>soulwarden-local-sts-session-token</SessionToken>"
        "<Expiration>2099-01-01T00:00:00Z</Expiration></Credentials></AssumeRoleResult>"
        "<ResponseMetadata><RequestId>soulwarden-local</RequestId></ResponseMetadata></AssumeRoleResponse>";
    return response;
}

/** Kinesis telemetry sink. Records are accepted and dropped. */
service::HttpResponse kinesis(const RouteContext& context) {
    const json body = json::parse(context.request.body, nullptr, false);
    json records = json::array();
    if (body.is_object() && body.contains("Records") && body["Records"].is_array()) {
        for (std::size_t index = 0; index < body["Records"].size(); ++index) {
            records.push_back({{"SequenceNumber", std::to_string(index + 1)}, {"ShardId", "shardId-000000000000"}});
        }
    }
    service::HttpResponse response;
    response.contentType = "application/x-amz-json-1.1";
    response.body = json{{"FailedRecordCount", 0}, {"Records", records}}.dump();
    return response;
}

} // namespace

void register_aws_routes(Router& router, const AwsOptions& options) {
    router.add("GET", "amazonaws.com", "/ping", "aws.ping", ping);
    if (options.meleeReport) {
        router.add("GET", kRemoteConfigurationHost, kGameplayConfigurationPath, "aws.s3_gameplay_configuration",
                   s3_gameplay_configuration);
    }
    router.add("GET", ".s3.", "/*", "aws.s3_missing", s3_missing);
    router.add("POST", "sts.", "/", "aws.sts", sts);
    router.add("POST", "kinesis.", "/", "aws.kinesis", kinesis);
}

} // namespace soulwarden::backend::server::http
