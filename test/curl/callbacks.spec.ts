/**
 * Copyright (c) Jonathan Cardoso Machado. All Rights Reserved.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
import { createServer } from '../helper/server'
import {
  Curl,
  CurlCode,
  CurlEasyError,
  CurlPreReqFunc,
  CurlSockAddr,
  CurlSockOpt,
  CurlSockType,
  Easy,
} from '../../lib'
import {
  describe,
  beforeAll,
  afterAll,
  beforeEach,
  afterEach,
  it,
  expect,
} from 'vitest'
import { withCommonTestOptions } from '../helper/commonOptions'

let curl: Curl
let serverInstance: ReturnType<typeof createServer>

describe('Callbacks', () => {
  beforeEach(() => {
    curl = new Curl()
    withCommonTestOptions(curl)
  })

  afterEach(() => {
    curl.close()
  })

  beforeAll(async () => {
    serverInstance = createServer()
    await serverInstance.listen()
  })

  beforeAll(() => {
    serverInstance.app.get('/delayed', (_req, res) => {
      const delayBetweenSends = 10
      const data = [
        '<html>',
        '<body>',
        '<h1>Hello, World!</h1>',
        '</body>',
        '</html>',
      ]
      const send = () => {
        const item = data.shift()

        if (!item) {
          res.end()
          return
        }

        res.write(item)
        setTimeout(send, delayBetweenSends)
      }

      send()
    })

    let trailers: { [key: string]: string | undefined }

    serverInstance.app.get('/trailers', (req, res) => {
      res.send({
        trailers,
      })
    })

    serverInstance.app.put('/headers', (req, res) => {
      req.resume()

      req.on('end', () => {
        trailers = req.trailers

        res.send({
          headers: req.headers,
          trailers: req.trailers,
        })
      })
    })
  })

  afterAll(async () => {
    await serverInstance.close()
    serverInstance.app._router.stack.pop()
    serverInstance.app._router.stack.pop()
    serverInstance.app._router.stack.pop()
  })

  describe('progress', () => {
    it('should work', async () => {
      let wasCalled = false

      curl.setOpt('URL', `${serverInstance.url}/delayed`)
      curl.setOpt('NOPROGRESS', false)

      curl.setProgressCallback((dltotal, dlnow, ultotal, ulnow) => {
        wasCalled = true
        expect(dltotal).toBeTypeOf('number')
        expect(dlnow).toBeTypeOf('number')
        expect(ultotal).toBeTypeOf('number')
        expect(ulnow).toBeTypeOf('number')
        return 0
      })

      await new Promise<void>((resolve, reject) => {
        curl.on('end', () => {
          expect(wasCalled).toBe(true)
          resolve()
        })

        curl.on('error', reject)

        curl.perform()
      })
    })

    it('should not accept undefined return', async () => {
      curl.setOpt('URL', `${serverInstance.url}/delayed`)
      curl.setOpt('NOPROGRESS', false)

      // @ts-ignore we want to test returning undefined here
      curl.setProgressCallback((_dltotal, dlnow, _ultotal, _ulnow) => {
        return dlnow >= 40 ? undefined : 0
      })

      await new Promise<void>((resolve, reject) => {
        curl.on('end', () => {
          reject(new Error('end called - request was not aborted by request'))
        })

        curl.on('error', (error: CurlEasyError, errorCode) => {
          try {
            expect(error).toBeInstanceOf(CurlEasyError)
            expect((error.cause as Error).message).toMatch(
              /Return value from the (.*) callback must be an integer/,
            )
            expect(error.code).toBe(CurlCode.CURLE_ABORTED_BY_CALLBACK)
            expect(error.code).toBe(errorCode)
            resolve()
          } catch (error) {
            reject(error)
          }
        })

        curl.perform()
      })
    }, 10000)
  }, 10000)

  describe.runIf(Curl.isVersionGreaterOrEqualThan(7, 64, 0))(
    'trailer',
    function () {
      it('should work', async () => {
        let wasCalled = false
        let isFirstCall = true

        curl.setOpt('URL', `${serverInstance.url}/headers`)
        curl.setOpt('UPLOAD', true)
        curl.setOpt('HTTPHEADER', ['x-random-header: random-value'])
        curl.setOpt('TRAILERFUNCTION', () => {
          wasCalled = true
          return ['x-random-header: random-value2']
        })

        let finished = false
        curl.setOpt(Curl.option.READFUNCTION, (buffer, _size, _nmemb) => {
          if (finished) return 0

          const data = 'HELLO'
          finished = true
          return buffer.write(data)
        })

        await new Promise<void>((resolve, reject) => {
          curl.on('end', (statusCode, body) => {
            try {
              if (isFirstCall) {
                expect(wasCalled).toBe(true)
                expect(statusCode).toBe(200)

                curl.setOpt('URL', `${serverInstance.url}/trailers`)
                curl.setOpt('UPLOAD', 0)
                curl.setOpt('HTTPHEADER', null)
                curl.setOpt('TRAILERFUNCTION', null)

                isFirstCall = false
                wasCalled = false

                curl.perform()
              } else {
                expect(wasCalled).toBe(false)
                expect(
                  JSON.parse(body as string).trailers['x-random-header'],
                ).toBe('random-value2')
                resolve()
              }
            } catch (error) {
              reject(error)
            }
          })

          curl.on('error', reject)

          curl.perform()
        })
      })

      it('should abort request on false', async () => {
        curl.setOpt('URL', `${serverInstance.url}/headers`)
        curl.setOpt('UPLOAD', true)
        curl.setOpt('HTTPHEADER', ['x-random-header: random-value'])
        curl.setOpt('TRAILERFUNCTION', () => {
          return false
        })

        let finished = false
        curl.setOpt(Curl.option.READFUNCTION, (buffer, _size, _nmemb) => {
          if (finished) return 0

          const data = 'HELLO'
          finished = true
          return buffer.write(data)
        })

        await new Promise<void>((resolve, reject) => {
          curl.on('end', () => {
            reject(new Error('end called - request was not aborted by request'))
          })

          curl.on('error', (error, errorCode) => {
            try {
              expect(errorCode).toBe(CurlCode.CURLE_ABORTED_BY_CALLBACK)
              resolve()
            } catch (error) {
              reject(error)
            }
          })

          curl.perform()
        })
      })

      it('should rethrow error thrown inside callback', async () => {
        curl.setOpt('URL', `${serverInstance.url}/headers`)
        curl.setOpt('UPLOAD', true)
        curl.setOpt('HTTPHEADER', ['x-random-header: random-value'])
        // @ts-ignore
        curl.setOpt('TRAILERFUNCTION', () => {
          throw new Error('thrown error inside callback')
        })

        let finished = false
        curl.setOpt(Curl.option.READFUNCTION, (buffer, _size, _nmemb) => {
          if (finished) return 0

          const data = 'HELLO'
          finished = true
          return buffer.write(data)
        })

        await new Promise<void>((resolve, reject) => {
          curl.on('end', () => {
            reject(new Error('end called - request was not aborted by request'))
          })

          curl.on('error', (error: CurlEasyError, errorCode) => {
            try {
              expect(error).toBeInstanceOf(CurlEasyError)
              expect((error.cause as Error).message).toBe(
                'thrown error inside callback',
              )
              expect(error.code).toBe(errorCode)
              expect(errorCode).toBe(CurlCode.CURLE_ABORTED_BY_CALLBACK)
              resolve()
            } catch (error) {
              reject(error)
            }
          })

          curl.perform()
        })
      })

      it('should throw an error on invalid return value', async () => {
        curl.setOpt('URL', `${serverInstance.url}/headers`)
        curl.setOpt('UPLOAD', true)
        curl.setOpt('HTTPHEADER', ['x-random-header: random-value'])
        // @ts-ignore
        curl.setOpt('TRAILERFUNCTION', () => {
          return {}
        })

        let finished = false
        curl.setOpt(Curl.option.READFUNCTION, (buffer, _size, _nmemb) => {
          if (finished) return 0

          const data = 'HELLO'
          finished = true
          return buffer.write(data)
        })

        await new Promise<void>((resolve, reject) => {
          curl.on('end', () => {
            reject(new Error('end called - request was not aborted by request'))
          })

          curl.on('error', (error: CurlEasyError, errorCode) => {
            try {
              expect(error).toBeInstanceOf(CurlEasyError)
              expect((error.cause as Error).message).toBe(
                'Return value from the Trailer callback must be an array of strings or false.',
              )
              expect(errorCode).toBe(CurlCode.CURLE_ABORTED_BY_CALLBACK)
              expect(error.code).toBe(errorCode)
              resolve()
            } catch (error) {
              reject(error)
            }
          })

          curl.perform()
        })
      })
    },
  )

  describe.runIf(Curl.isVersionGreaterOrEqualThan(7, 80, 0))(
    'prereq',
    function () {
      it('should work', async () => {
        let wasCalled = false

        curl.setOpt('URL', `${serverInstance.url}/headers`)
        curl.setOpt('PREREQFUNCTION', () => {
          wasCalled = true
          return CurlPreReqFunc.Ok
        })

        await new Promise<void>((resolve, reject) => {
          curl.on('end', () => {
            try {
              expect(wasCalled).toBe(true)
              resolve()
            } catch (error) {
              reject(error)
            }
          })

          curl.on('error', reject)

          curl.perform()
        })
      })

      it('should abort request on Abort return value', async () => {
        let wasCalled = false

        curl.setOpt('URL', `${serverInstance.url}/headers`)
        curl.setOpt('PREREQFUNCTION', () => {
          wasCalled = true
          return CurlPreReqFunc.Abort
        })

        await new Promise<void>((resolve, reject) => {
          curl.on('end', () => {
            reject(new Error('end called - request was not aborted by request'))
          })

          curl.on('error', (_error, errorCode) => {
            try {
              expect(wasCalled).toBe(true)
              expect(errorCode).toBe(CurlCode.CURLE_ABORTED_BY_CALLBACK)
              resolve()
            } catch (error) {
              reject(error)
            }
          })

          curl.perform()
        })
      })

      it('should rethrow error thrown inside callback', async () => {
        let wasCalled = false
        let errorObject!: Error
        curl.setOpt('URL', `${serverInstance.url}/headers`)
        curl.setOpt('PREREQFUNCTION', () => {
          wasCalled = true
          errorObject = new Error('thrown error inside callback')
          throw errorObject
        })

        await new Promise<void>((resolve, reject) => {
          curl.on('end', () => {
            reject(new Error('end called - request was not aborted by request'))
          })

          curl.on('error', (error: CurlEasyError, errorCode) => {
            try {
              expect(wasCalled).toBe(true)
              expect(error).toBeInstanceOf(CurlEasyError)
              expect(error.cause).toBe(errorObject)
              expect(errorCode).toBe(CurlCode.CURLE_ABORTED_BY_CALLBACK)
              expect(error.code).toBe(errorCode)
              resolve()
            } catch (error) {
              reject(error)
            }
          })

          curl.perform()
        })
      })

      it('should throw an error on invalid return value', async () => {
        let wasCalled = false

        curl.setOpt('URL', `${serverInstance.url}/headers`)
        // @ts-expect-error this should require the fn to return an enum value!
        curl.setOpt('PREREQFUNCTION', () => {
          wasCalled = true
          return '123'
        })

        await new Promise<void>((resolve, reject) => {
          curl.on('end', () => {
            reject(new Error('end called - request was not aborted by request'))
          })

          curl.on('error', (error, errorCode) => {
            try {
              expect(wasCalled).toBe(true)
              expect(error).toBeInstanceOf(CurlEasyError)
              expect((error.cause as Error).message).toBe(
                'Return value from the PREREQ callback must be a number.',
              )
              expect(errorCode).toBe(CurlCode.CURLE_ABORTED_BY_CALLBACK)
              resolve()
            } catch (error) {
              reject(error)
            }
          })

          curl.perform()
        })
      })
    },
  )
  describe('sockopt', function () {
    // The callback is only called when a new socket is created, make sure
    // a connection from a previous test is not reused.
    beforeEach(() => {
      curl.setOpt('FRESH_CONNECT', true)
    })

    const performAndWait = () =>
      new Promise<{ error?: CurlEasyError; errorCode?: CurlCode }>(
        (resolve) => {
          curl.on('end', () => resolve({}))
          curl.on('error', (error: CurlEasyError, errorCode: CurlCode) =>
            resolve({ error, errorCode }),
          )
          curl.perform()
        },
      )

    it('should be called with the fd, purpose and destination address', async () => {
      const calls: Array<{
        fd: number
        purpose: CurlSockType
        address: CurlSockAddr | undefined
        thisArg: unknown
      }> = []

      curl.setOpt('URL', `${serverInstance.url}/headers`)
      curl.setOpt('SOCKOPTFUNCTION', function (fd, purpose, address) {
        calls.push({ fd, purpose, address, thisArg: this })
        return CurlSockOpt.Ok
      })

      const { error } = await performAndWait()

      expect(error).toBeUndefined()
      expect(calls.length).toBeGreaterThanOrEqual(1)

      const [call] = calls
      expect(call.thisArg).toBeInstanceOf(Easy)
      expect(typeof call.fd).toBe('number')
      expect(call.purpose).toBe(CurlSockType.IpCxn)
      expect(call.address).toBeDefined()
      expect(['IPv4', 'IPv6']).toContain(call.address!.family)
      expect(['127.0.0.1', '::1']).toContain(call.address!.address)
      expect(call.address!.port).toBe(serverInstance.port)
    })

    it('should abort the connection on Error return value (SSRF fence)', async () => {
      const isLoopback = (address?: CurlSockAddr) =>
        !!address &&
        (address.address.startsWith('127.') || address.address === '::1')

      let wasCalled = false
      curl.setOpt('URL', `${serverInstance.url}/headers`)
      curl.setOpt('SOCKOPTFUNCTION', (_fd, _purpose, address) => {
        wasCalled = true
        return isLoopback(address) ? CurlSockOpt.Error : CurlSockOpt.Ok
      })

      const { error, errorCode } = await performAndWait()

      expect(wasCalled).toBe(true)
      expect(error).toBeInstanceOf(CurlEasyError)
      expect(errorCode).toBe(CurlCode.CURLE_ABORTED_BY_CALLBACK)
    })

    it('should rethrow error thrown inside callback', async () => {
      const errorObject = new Error('thrown error inside callback')
      curl.setOpt('URL', `${serverInstance.url}/headers`)
      curl.setOpt('SOCKOPTFUNCTION', () => {
        throw errorObject
      })

      const { error, errorCode } = await performAndWait()

      expect(error).toBeInstanceOf(CurlEasyError)
      expect(error!.cause).toBe(errorObject)
      expect(errorCode).toBe(CurlCode.CURLE_ABORTED_BY_CALLBACK)
    })

    it('should throw an error on invalid return value', async () => {
      curl.setOpt('URL', `${serverInstance.url}/headers`)
      // @ts-expect-error this should require the fn to return an enum value!
      curl.setOpt('SOCKOPTFUNCTION', () => 'abc')

      const { error, errorCode } = await performAndWait()

      expect(error).toBeInstanceOf(CurlEasyError)
      expect((error!.cause as Error).message).toBe(
        'Return value from the SOCKOPT callback must be a number.',
      )
      expect(errorCode).toBe(CurlCode.CURLE_ABORTED_BY_CALLBACK)
    })

    it('should stop being called after being unset', async () => {
      let wasCalled = false
      curl.setOpt('URL', `${serverInstance.url}/headers`)
      curl.setOpt('SOCKOPTFUNCTION', () => {
        wasCalled = true
        return CurlSockOpt.Error
      })
      curl.setOpt('SOCKOPTFUNCTION', null)

      const { error } = await performAndWait()

      expect(error).toBeUndefined()
      expect(wasCalled).toBe(false)
    })

    it('should work with the Easy interface and duplicated handles', () => {
      const easy = new Easy()
      withCommonTestOptions(easy)
      const callers: Easy[] = []

      easy.setOpt('URL', `${serverInstance.url}/headers`)
      easy.setOpt('FRESH_CONNECT', true)
      easy.setOpt('SOCKOPTFUNCTION', function () {
        callers.push(this)
        return CurlSockOpt.Error
      })

      const dup = easy.dupHandle()

      expect(easy.perform()).toBe(CurlCode.CURLE_ABORTED_BY_CALLBACK)
      expect(dup.perform()).toBe(CurlCode.CURLE_ABORTED_BY_CALLBACK)
      // localhost may resolve to more than one address, each attempt calls the callback
      const easyCalls = callers.filter((caller) => caller === easy).length
      const dupCalls = callers.filter((caller) => caller === dup).length
      expect(easyCalls).toBeGreaterThanOrEqual(1)
      expect(dupCalls).toBeGreaterThanOrEqual(1)
      expect(easyCalls + dupCalls).toBe(callers.length)

      easy.close()
      dup.close()
    })
  })
}, 5000)
