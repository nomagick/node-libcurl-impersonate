/**
 * Copyright (c) Jonathan Cardoso Machado. All Rights Reserved.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
// https://github.com/curl/curl/blob/curl-8_17_0/include/curl/curl.h#L395
/**
 * Object to be used as the return value for the callback set with the option `SOCKOPTFUNCTION`
 *
 * `CURL_SOCKOPT_OK` becomes `CurlSockOpt.Ok`
 *
 * Returning `CurlSockOpt.Error` aborts the connection attempt with
 * `CurlCode.CURLE_ABORTED_BY_CALLBACK`.
 *
 * @public
 */
export enum CurlSockOpt {
  Ok = 0,
  Error = 1,
  AlreadyConnected = 2,
}

/**
 * The `purpose` argument passed to the callback set with the option `SOCKOPTFUNCTION`
 *
 * `CURLSOCKTYPE_IPCXN` becomes `CurlSockType.IpCxn`
 *
 * @public
 */
export enum CurlSockType {
  /**
   * Socket created for a specific IP connection (not connected yet)
   */
  IpCxn = 0,
  /**
   * Socket created by accept() call (FTP active mode)
   */
  Accept = 1,
}

/**
 * Address the socket passed to the `SOCKOPTFUNCTION` callback is about to connect to
 * (or is connected to, for `CurlSockType.Accept`).
 *
 * This is the address of the actual peer of the TCP/UDP connection, so if a proxy is
 * being used, it is the address of the proxy.
 *
 * @public
 */
export interface CurlSockAddr {
  family: 'IPv4' | 'IPv6' | 'unix'
  address: string
  port: number
}
