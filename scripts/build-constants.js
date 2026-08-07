const fs = require('fs')
const path = require('path')
const { inspect } = require('util')
const { execSync } = require('child_process')

const { optionKindMap, optionKindValueMap } = require('./data/options')

const {
  createConstantsFile,
  getDescriptionCommentForOption,
} = require('./utils/createConstantsFile')
const { createSetOptOverloads } = require('./utils/createSetOptOverloads')
const { curlOptionsBlacklist } = require('./utils/curlOptionsBlacklist')
const { multiOptionsBlacklist } = require('./utils/multiOptionsBlacklist')
const { retrieveConstantList } = require('./utils/retrieveConstantList')

const run = async () => {
  const curlOptionsFilePath = path.resolve(
    __dirname,
    '../lib/generated/CurlOption.ts',
  )

  const curlInfoFilePath = path.resolve(
    __dirname,
    '../lib/generated/CurlInfo.ts',
  )

  const multiOptionFilePath = path.resolve(
    __dirname,
    '../lib/generated/MultiOption.ts',
  )

  const allowedCurlOptions = await retrieveConstantList({
    url: 'https://curl.se/libcurl/c/curl_easy_setopt.html',
    constantPrefix: 'CURLOPT_',
    blacklist: curlOptionsBlacklist,
  })

  allowedCurlOptions.push(
    ...[
      {
        constantOriginal: 'CURLOPT_IMPERSONATE',
        constantName: 'IMPERSONATE',
        constantNameCamelCase: 'impersonate',
        description:
          'curl-impersonate: The master option for setting an impersonate target',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/2447279b1a388ba907f4bfe4fe02aadd1dc24376/patches/curl.patch#L107',
      },
      {
        constantOriginal: 'CURLOPT_HTTPBASEHEADER',
        constantName: 'HTTPBASEHEADER',
        constantNameCamelCase: 'httpBaseHeader',
        description:
          'curl-impersonate: A list of headers used by the impersonated browser. ' +
          'If given, merged with CURLOPT_HTTPHEADER.',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/2447279b1a388ba907f4bfe4fe02aadd1dc24376/patches/curl.patch#L111',
      },
      {
        constantOriginal: 'CURLOPT_SSL_SIG_HASH_ALGS',
        constantName: 'SSL_SIG_HASH_ALGS',
        constantNameCamelCase: 'sslSigHashAlgs',
        description:
          'curl-impersonate: A list of TLS signature hash algorithms. ' +
          'This has been implemented by curl as option 328, but we will keep it for compatibility. ' +
          'See https://datatracker.ietf.org/doc/html/rfc5246#section-7.4.1.4.1.',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/2447279b1a388ba907f4bfe4fe02aadd1dc24376/patches/curl.patch#L116',
      },
      {
        constantOriginal: 'CURLOPT_SSL_ENABLE_ALPS',
        constantName: 'SSL_ENABLE_ALPS',
        constantNameCamelCase: 'sslEnableAlps',
        description:
          'curl-impersonate: Whether to enable ALPS in TLS or not. ' +
          'See https://datatracker.ietf.org/doc/html/draft-vvv-tls-alps. ' +
          'Support for ALPS is minimal and is intended only for the TLS client hello to match.',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/2447279b1a388ba907f4bfe4fe02aadd1dc24376/patches/curl.patch#L122',
      },
      {
        constantOriginal: 'CURLOPT_SSL_CERT_COMPRESSION',
        constantName: 'SSL_CERT_COMPRESSION',
        constantNameCamelCase: 'sslCertCompression',
        description:
          'curl-impersonate: : Comma-separated list of certificate compression algorithms to use. ' +
          'These are published in the client hello. Supported algorithms are "zlib" and "brotli". ' +
          'See https://datatracker.ietf.org/doc/html/rfc8879.',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/2447279b1a388ba907f4bfe4fe02aadd1dc24376/patches/curl.patch#L128',
      },
      {
        constantOriginal: 'CURLOPT_SSL_ENABLE_TICKET',
        constantName: 'SSL_ENABLE_TICKET',
        constantNameCamelCase: 'sslEnableTicket',
        description:
          'curl-impersonate: Enable/disable TLS session ticket extension (RFC5077).',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/2447279b1a388ba907f4bfe4fe02aadd1dc24376/patches/curl.patch#L131',
      },
      {
        constantOriginal: 'CURLOPT_HTTP2_PSEUDO_HEADERS_ORDER',
        constantName: 'HTTP2_PSEUDO_HEADERS_ORDER',
        constantNameCamelCase: 'http2PseudoHeadersOrder',
        description:
          'curl-impersonate: Set the order of the HTTP/2 pseudo headers. ' +
          "The value must contain the letters 'm', 'a', 's', 'p' " +
          'representing the pseudo-headers ":method", ":authority", ":scheme", ":path" ' +
          'in the desired order of appearance in the HTTP/2 HEADERS frame.',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/2447279b1a388ba907f4bfe4fe02aadd1dc24376/patches/curl.patch#L140',
      },
      {
        constantOriginal: 'CURLOPT_HTTP2_SETTINGS',
        constantName: 'HTTP2_SETTINGS',
        constantNameCamelCase: 'http2Settings',
        description:
          'curl-impersonate: HTTP2 settings frame keys and values, format: 1:v;2:v;3:v.',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/2447279b1a388ba907f4bfe4fe02aadd1dc24376/patches/curl.patch#L143',
      },
      {
        constantOriginal: 'CURLOPT_SSL_PERMUTE_EXTENSIONS',
        constantName: 'SSL_PERMUTE_EXTENSIONS',
        constantNameCamelCase: 'sslPermuteExtensions',
        description:
          'curl-impersonate: Whether to enable Boringssl permute extensions. ' +
          'See https://commondatastorage.googleapis.com/chromium-boringssl-docs/ssl.h.html#SSL_set_permute_extensions.',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/2447279b1a388ba907f4bfe4fe02aadd1dc24376/patches/curl.patch#L149',
      },
      {
        constantOriginal: 'CURLOPT_HTTP2_WINDOW_UPDATE',
        constantName: 'HTTP2_WINDOW_UPDATE',
        constantNameCamelCase: 'http2WindowUpdate',
        description: 'curl-impersonate: HTTP2 initial window update.',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/2447279b1a388ba907f4bfe4fe02aadd1dc24376/patches/curl.patch#L152',
      },
      {
        constantOriginal: 'CURLOPT_HTTP2_STREAMS',
        constantName: 'HTTP2_STREAMS',
        constantNameCamelCase: 'http2Streams',
        description:
          'curl-impersonate: Set the initial streams settings for http2.',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/2447279b1a388ba907f4bfe4fe02aadd1dc24376/patches/curl.patch#L155',
      },
      {
        constantOriginal: 'CURLOPT_TLS_GREASE',
        constantName: 'TLS_GREASE',
        constantNameCamelCase: 'tlsGrease',
        description: 'curl-impersonate: enable tls grease.',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/2447279b1a388ba907f4bfe4fe02aadd1dc24376/patches/curl.patch#L158',
      },
      {
        constantOriginal: 'CURLOPT_TLS_EXTENSION_ORDER',
        constantName: 'TLS_EXTENSION_ORDER',
        constantNameCamelCase: 'tlsExtensionOrder',
        description: 'curl-impersonate: set tls extension order.',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/2447279b1a388ba907f4bfe4fe02aadd1dc24376/patches/curl.patch#L161',
      },
      {
        constantOriginal: 'CURLOPT_STREAM_EXCLUSIVE',
        constantName: 'STREAM_EXCLUSIVE',
        constantNameCamelCase: 'streamExclusive',
        description: 'curl-impersonate: Set stream exclusiveness, 0 or 1.',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/2447279b1a388ba907f4bfe4fe02aadd1dc24376/patches/curl.patch#L164',
      },
      {
        constantOriginal: 'CURLOPT_TLS_KEY_USAGE_NO_CHECK',
        constantName: 'TLS_KEY_USAGE_NO_CHECK',
        constantNameCamelCase: 'tlsKeyUsageNoCheck',
        description:
          'curl-impersonate: enable tls key usage check, defaults: on.',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/2447279b1a388ba907f4bfe4fe02aadd1dc24376/patches/curl.patch#L167',
      },
      {
        constantOriginal: 'CURLOPT_TLS_SIGNED_CERT_TIMESTAMPS',
        constantName: 'TLS_SIGNED_CERT_TIMESTAMPS',
        constantNameCamelCase: 'tlsSignedCertTimestamps',
        description: 'curl-impersonate: enable tls signed cert stamps.',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/2447279b1a388ba907f4bfe4fe02aadd1dc24376/patches/curl.patch#L170',
      },
      {
        constantOriginal: 'CURLOPT_TLS_STATUS_REQUEST',
        constantName: 'TLS_STATUS_REQUEST',
        constantNameCamelCase: 'tlsStatusRequest',
        description: 'curl-impersonate: enable tls status request.',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/2447279b1a388ba907f4bfe4fe02aadd1dc24376/patches/curl.patch#L173',
      },
      {
        constantOriginal: 'CURLOPT_TLS_DELEGATED_CREDENTIALS',
        constantName: 'TLS_DELEGATED_CREDENTIALS',
        constantNameCamelCase: 'tlsDelegatedCredentials',
        description: 'curl-impersonate: firefox delegated credentials.',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/2447279b1a388ba907f4bfe4fe02aadd1dc24376/patches/curl.patch#L176',
      },
      {
        constantOriginal: 'CURLOPT_TLS_RECORD_SIZE_LIMIT',
        constantName: 'TLS_RECORD_SIZE_LIMIT',
        constantNameCamelCase: 'tlsRecordSizeLimit',
        description: 'curl-impersonate: firefox record size limit.',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/2447279b1a388ba907f4bfe4fe02aadd1dc24376/patches/curl.patch#L179',
      },
      {
        constantOriginal: 'CURLOPT_TLS_KEY_SHARES_LIMIT',
        constantName: 'TLS_KEY_SHARES_LIMIT',
        constantNameCamelCase: 'tlsKeySharesLimit',
        description: 'curl-impersonate: firefox key_shares_limit.',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/2447279b1a388ba907f4bfe4fe02aadd1dc24376/patches/curl.patch#L182',
      },
      {
        constantOriginal: 'CURLOPT_TLS_USE_NEW_ALPS_CODEPOINT',
        constantName: 'TLS_USE_NEW_ALPS_CODEPOINT',
        constantNameCamelCase: 'tlsUseNewAlpsCodepoint',
        description: 'curl-impersonate: : Use the new ALPS code point.',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/2447279b1a388ba907f4bfe4fe02aadd1dc24376/patches/curl.patch#L185',
      },
      {
        constantOriginal: 'CURLOPT_HTTP2_NO_PRIORITY',
        constantName: 'HTTP2_NO_PRIORITY',
        constantNameCamelCase: 'http2NoPriority',
        description:
          'curl-impersonate: Do not set the priority bit in http2 header frame.',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/2447279b1a388ba907f4bfe4fe02aadd1dc24376/patches/curl.patch#L188',
      },
      {
        constantOriginal: 'CURLOPT_PROXY_CREDENTIAL_NO_REUSE',
        constantName: 'PROXY_CREDENTIAL_NO_REUSE',
        constantNameCamelCase: 'proxyCredentialNoReuse',
        description:
          'curl-impersonate: Do not reuse TLS sessions or connections from different proxy credentials.',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/2447279b1a388ba907f4bfe4fe02aadd1dc24376/patches/curl.patch#L191',
      },
      {
        constantOriginal: 'CURLOPT_SPLIT_COOKIES',
        constantName: 'SPLIT_COOKIES',
        constantNameCamelCase: 'splitCookies',
        description:
          'curl-impersonate: Split cookies into separate header lines.',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/8e674f96ebae13cdc97b488abb45283e99bd3571/patches/curl.patch#L895',
      },
      {
        constantOriginal: 'CURLOPT_FORM_BOUNDARY',
        constantName: 'FORM_BOUNDARY',
        constantNameCamelCase: 'formBoundary',
        description:
          'curl-impersonate: Set the form boundary string for HTTP POST requests.',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/8e674f96ebae13cdc97b488abb45283e99bd3571/patches/curl.patch#L898',
      },
      {
        constantOriginal: 'CURLOPT_HTTP3_PSEUDO_HEADERS_ORDER',
        constantName: 'HTTP3_PSEUDO_HEADERS_ORDER',
        constantNameCamelCase: 'http3PseudoHeadersOrder',
        description:
          'curl-impersonate: Set the order of the HTTP/3 pseudo headers. ' +
          "The value must contain the letters 'm', 'a', 's', 'p' " +
          'representing the pseudo-headers ":method", ":authority", ":scheme", ":path" ' +
          'in the desired order of appearance in the HTTP/3 HEADERS frame.',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/8e674f96ebae13cdc97b488abb45283e99bd3571/patches/curl.patch#L909',
      },
      {
        constantOriginal: 'CURLOPT_HTTP3_SETTINGS',
        constantName: 'HTTP3_SETTINGS',
        constantNameCamelCase: 'http3Settings',
        description:
          'curl-impersonate: HTTP3 settings frame keys and values, format: 1:v;6:v;7:v',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/8e674f96ebae13cdc97b488abb45283e99bd3571/patches/curl.patch#L912',
      },
      {
        constantOriginal: 'CURLOPT_QUIC_CID_LENGTH',
        constantName: 'QUIC_CID_LENGTH',
        constantNameCamelCase: 'quicCidLength',
        description:
          'curl-impersonate: Sets the browser profile used to choose the initial QUIC destination and source connection ID lengths.',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/ddf2c1a44fa991315d9ffbc7154bb196e143726d/patches/curl.patch#L952',
      },
      {
        constantOriginal: 'CURLOPT_QUIC_TRANSPORT_PARAMETERS',
        constantName: 'QUIC_TRANSPORT_PARAMETERS',
        constantNameCamelCase: 'quicTransportParameters',
        description:
          'curl-impersonate: QUIC transport parameters, format: id:value;id:value',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/8e674f96ebae13cdc97b488abb45283e99bd3571/patches/curl.patch#L915',
      },
      {
        constantOriginal: 'CURLOPT_HTTP3_SIG_HASH_ALGS',
        constantName: 'HTTP3_SIG_HASH_ALGS',
        constantNameCamelCase: 'http3SigHashAlgs',
        description:
          'curl-impersonate: Signature hash algorithms for HTTP/3 (QUIC TLS).',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/8e674f96ebae13cdc97b488abb45283e99bd3571/patches/curl.patch#L920',
      },
      {
        constantOriginal: 'CURLOPT_HTTP3_TLS_EXTENSION_ORDER',
        constantName: 'HTTP3_TLS_EXTENSION_ORDER',
        constantNameCamelCase: 'http3TlsExtensionOrder',
        description:
          'curl-impersonate: TLS extension order for HTTP/3 (QUIC TLS).',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/8e674f96ebae13cdc97b488abb45283e99bd3571/patches/curl.patch#L924',
      },
      {
        constantOriginal: 'CURLOPT_HTTPHEADER_ORDER',
        constantName: 'HTTPHEADER_ORDER',
        constantNameCamelCase: 'httpheaderOrder',
        description:
          'curl-impersonate: Comma-separated order for normal HTTP headers.',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/8e674f96ebae13cdc97b488abb45283e99bd3571/patches/curl.patch#L927',
      },
      {
        constantOriginal: 'CURLOPT_HTTP3_HTTPHEADER',
        constantName: 'HTTP3_HTTPHEADER',
        constantNameCamelCase: 'http3Httpheader',
        description: 'curl-impersonate: HTTP/3-specific custom HTTP headers.',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/8e674f96ebae13cdc97b488abb45283e99bd3571/patches/curl.patch#L931',
      },
      {
        constantOriginal: 'CURLOPT_HTTP3_HTTPHEADER_ORDER',
        constantName: 'HTTP3_HTTPHEADER_ORDER',
        constantNameCamelCase: 'http3HttpheaderOrder',
        description:
          'curl-impersonate: HTTP/3-specific order for normal HTTP headers.',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/8e674f96ebae13cdc97b488abb45283e99bd3571/patches/curl.patch#L934',
      },
      {
        constantOriginal: 'CURLOPT_HTTP3_SSL_EC_CURVES',
        constantName: 'HTTP3_SSL_EC_CURVES',
        constantNameCamelCase: 'http3SslEcCurves',
        description: 'curl-impersonate: HTTP/3-specific EC curves.',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/8e674f96ebae13cdc97b488abb45283e99bd3571/patches/curl.patch#L937',
      },
      {
        constantOriginal: 'CURLOPT_WS_HTTPHEADER',
        constantName: 'WS_HTTPHEADER',
        constantNameCamelCase: 'wsHttpheader',
        description:
          'curl-impersonate: WebSocket-specific custom HTTP headers.',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/8e674f96ebae13cdc97b488abb45283e99bd3571/patches/curl.patch#L941',
      },
      {
        constantOriginal: 'CURLOPT_WS_HTTPHEADER_ORDER',
        constantName: 'WS_HTTPHEADER_ORDER',
        constantNameCamelCase: 'wsHttpheaderOrder',
        description:
          'curl-impersonate: WebSocket-specific order for custom HTTP headers.',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/8e674f96ebae13cdc97b488abb45283e99bd3571/patches/curl.patch#L944',
      },
      {
        constantOriginal: 'CURLOPT_WS_SSL_DISABLE_TICKET',
        constantName: 'WS_SSL_DISABLE_TICKET',
        constantNameCamelCase: 'wsSslDisableTicket',
        description:
          'curl-impersonate: Disable WebSocket TLS session ticket extension.',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/8e674f96ebae13cdc97b488abb45283e99bd3571/patches/curl.patch#L947',
      },
      {
        constantOriginal: 'CURLOPT_WS_SSL_CERT_COMPRESSION',
        constantName: 'WS_SSL_CERT_COMPRESSION',
        constantNameCamelCase: 'wsSslCertCompression',
        description:
          'curl-impersonate: WebSocket-specific TLS certificate compression.',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/8e674f96ebae13cdc97b488abb45283e99bd3571/patches/curl.patch#L950',
      },
    ],
  )

  allowedCurlOptions.sort((a, b) =>
    a.constantName.localeCompare(b.constantName),
  )

  await createConstantsFile({
    constants: allowedCurlOptions,
    variableName: 'CurlOption',
    filePath: curlOptionsFilePath,
    shouldGenerateCamelCaseMap: true,
    extraHeaderText: `
      import { CurlChunk } from '../enum/CurlChunk'
      import { CurlFnMatchFunc } from '../enum/CurlFnMatchFunc'
      import { CurlFtpMethod } from '../enum/CurlFtpMethod'
      import { CurlFtpSsl } from '../enum/CurlFtpSsl'
      import { CurlGssApi } from '../enum/CurlGssApi'
      import { CurlHeader } from '../enum/CurlHeader'
      import { CurlHsts, CurlHstsCacheCount, CurlHstsCacheEntry } from '../enum/CurlHsts'
      import { CurlHttpVersion } from '../enum/CurlHttpVersion'
      import { CurlInfoDebug } from '../enum/CurlInfoDebug'
      import { CurlIpResolve } from '../enum/CurlIpResolve'
      import { CurlMimeOpt } from '../enum/CurlMimeOpt'
      import { CurlNetrc } from '../enum/CurlNetrc'
      import { CurlPreReqFunc } from '../enum/CurlPreReqFunc'
      import { CurlProgressFunc } from '../enum/CurlProgressFunc'
      import { CurlProtocol } from '../enum/CurlProtocol'
      import { CurlProxy } from '../enum/CurlProxy'
      import { CurlRtspRequest } from '../enum/CurlRtspRequest'
      import { CurlSshAuth } from '../enum/CurlSshAuth'
      import { CurlSshKeyType, CurlSshKeyMatch } from '../enum/CurlSshKey'
      import { CurlSslOpt } from '../enum/CurlSslOpt'
      import { CurlSslVersion } from '../enum/CurlSslVersion'
      import { CurlTimeCond } from '../enum/CurlTimeCond'
      import { CurlUseSsl } from '../enum/CurlUseSsl'
      import { CurlWsOptions } from '../enum/CurlWs'
      import { Easy } from "../Easy"
      import { Share } from "../Share"
      import { CurlMime } from "../CurlMime"
    `,
  })

  const allowedCurlInfos = await retrieveConstantList({
    url: 'https://curl.se/libcurl/c/curl_easy_getinfo.html',
    constantPrefix: 'CURLINFO_',
    blacklist: [
      // time constants at the bottom
      'NAMELOOKUP',
      'CONNECT',
      'APPCONNECT',
      'PRETRANSFER',
      'STARTTRANSFER',
      'TOTAL',
      'REDIRECT',
    ],
  })
  allowedCurlInfos.push(
    ...[
      {
        constantOriginal: 'CURLINFO_COOKIECHANGES',
        constantName: 'COOKIECHANGES',
        constantNameCamelCase: 'cookieChanges',
        description:
          'curl-impersonate: accepted server cookie mutations from the latest transfer, ' +
          'formatted as SET/DELETE plus Netscape cookie fields. Use a high number to avoid collisions with upstream infos.',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/8e674f96ebae13cdc97b488abb45283e99bd3571/patches/curl.patch#L963',
      },
      {
        constantOriginal: 'CURLINFO_REDIRECT_HISTORY',
        constantName: 'REDIRECT_HISTORY',
        constantNameCamelCase: 'redirectHistory',
        description:
          'curl-impersonate: followed HTTP redirect responses from the latest transfer, formatted as status-code plus tab plus request URL.',
        url: 'https://github.com/lexiforest/curl-impersonate/blob/8e674f96ebae13cdc97b488abb45283e99bd3571/patches/curl.patch#L967',
      },
    ],
  )
  await createConstantsFile({
    constants: allowedCurlInfos,
    variableName: 'CurlInfo',
    filePath: curlInfoFilePath,
  })

  const allowedMultiOptions = await retrieveConstantList({
    url: 'https://curl.se/libcurl/c/curl_multi_setopt.html',
    constantPrefix: 'CURLMOPT_',
    blacklist: multiOptionsBlacklist,
  })
  await createConstantsFile({
    constants: allowedMultiOptions,
    variableName: 'MultiOption',
    filePath: multiOptionFilePath,
  })

  // add extra types to CurlOption
  const union = (arr) => arr.map((i) => inspect(i)).join(' | ')

  let optionsValueTypeData = [
    'import { FileInfo, HttpPostField } from "../types"',
    `export type DataCallbackOptions = ${union(optionKindMap.dataCallback)}`,
    `export type ProgressCallbackOptions = ${union(
      optionKindMap.progressCallback,
    )}`,
    `export type StringListOptions = ${union(optionKindMap.stringList)}`,
    `export type BlobOptions = ${union(optionKindMap.blob)}`,
    `export type SpecificOptions = DataCallbackOptions | ProgressCallbackOptions | StringListOptions | BlobOptions | ${union(
      optionKindMap.other,
    )}`,
  ]

  // Now we must create the type for the curl.<http-verb> options param
  optionsValueTypeData = [
    ...optionsValueTypeData,
    `
    /**
     * @public
     */
    export type CurlOptionValueType = {`,
  ]

  for (const option of allowedCurlOptions) {
    const optionDescription = getDescriptionCommentForOption(option)

    const optionValueType =
      Object.entries(optionKindMap).reduce((acc, [kind, kindOptions]) => {
        if (acc) return acc

        return (
          kindOptions.includes(option.constantName) &&
          (optionKindValueMap[kind] || optionKindValueMap[option.constantName])
        )
      }, null) || optionKindValueMap._

    optionsValueTypeData = [
      ...optionsValueTypeData,
      `${optionDescription}${option.constantName}?: ${optionValueType} | null`,
      `${optionDescription}${option.constantNameCamelCase}?: ${optionValueType} | null`,
    ]
  }

  optionsValueTypeData = [...optionsValueTypeData, '}']

  fs.writeFileSync(curlOptionsFilePath, optionsValueTypeData.join('\n'), {
    flag: 'a+',
  })

  const easyBindingFilePath = path.resolve(__dirname, '../lib/Easy.ts')
  const curlClassFilePath = path.resolve(__dirname, '../lib/Curl.ts')

  createSetOptOverloads(easyBindingFilePath)
  createSetOptOverloads(curlClassFilePath, 'this')

  execSync(
    `pnpm prettier ${curlOptionsFilePath} ${curlInfoFilePath} ${multiOptionFilePath} ${easyBindingFilePath} ${curlClassFilePath}`,
  )
}

run()
