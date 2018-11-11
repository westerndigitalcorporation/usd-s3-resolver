# Usage of the URI Resolver

Currently the URI Resolver provides support for an alternative protocol, when referencing other usd files, namely loading data from an S3 object store.

## S3 Protocol

#### Request URL

S3 protocol can be accessed by using s3:<bucket_name>/<object_name> when referencing assets in a USD file.

```
usdcat s3:hello/world.usdz
```

#### Environment variables supported by the resolver

The S3 resolver supports the following environment variables.

- USD_S3_PROXY_HOST - Proxy host for S3 access, should point to an ActiveScale system node.
- USD_S3_PROXY_PORT - Proxy port for S3 access, defaults to port 80 for the HTTP scheme.
- USD_S3_CACHE_PATH - Name of the local cache path to save usd files. Default value is /tmp.

Create the S3 credentials in `~/.aws/credentials` with
```
aws configure
```

You can make use of AWS cli profiles. More info [here](https://docs.aws.amazon.com/cli/latest/userguide/cli-multiple-profiles.html).
```
export AWS_PROFILE=user2
```

#### Payload conversion

Example script to convert the payloads in the kitchen set to s3 urls and upload them to an s3 bucket on an ActiveScale endpoint.
```
cd kitchen
find ./assets -name "*payload.usd" -exec sed -i 's#references = @./#references = @s3:kitchen/#' {} \;

export EP="--endpoint-url http://system-node-ipaddress"
aws s3 mb s3://kitchen ${EP}
find ./assets -name "*geom.usd" -exec aws s3 cp {} s3://kitchen ${EP} \;
```

#### Versioning

Enable versioning on a bucket
```
aws s3api put-bucket-versioning --bucket kitchen --versioning-configuration Status=Enabled ${EP}
```
Any objects uploaded to this bucket are now versioned. They can be fetched as follows
```
usdview s3://hello/kitchen.usdz?versionId=FmpErZBtDpMNI3YZkcm1UjxJ_91yFQJUcUtL0Gtr8gPnLWfK"
```