/*
 * XNetworking Integration Tests
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "../test_fixtures.h"

#include <windows.h>
#include <xasync.h>
#include <xgameruntimeinit.h>
#include <xnetworking.h>
#include <xtaskqueue.h>

#include <vector>

namespace
{
    constexpr DWORD kWaitMs = 10000;

    void CALLBACK PortChanged( void *, UINT64 ) {}
    void CALLBACK HintChanged( void *, const XNetworkingConnectivityHint * ) {}

    /* Runs one query on a manual-dispatch queue until its completion callback fires. */
    struct Query
    {
        XAsyncBlock block{};
        XTaskQueueHandle queue = nullptr;
        HANDLE done = CreateEventA( nullptr, TRUE, FALSE, nullptr );

        Query()
        {
            XTaskQueueCreate( XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &queue );
            block.queue = queue;
            block.context = this;
            block.callback = []( XAsyncBlock *b ) { SetEvent( static_cast<Query *>( b->context )->done ); };
        }

        ~Query()
        {
            XTaskQueueCloseHandle( queue );
            CloseHandle( done );
        }

        bool Drain()
        {
            DWORD start = GetTickCount();
            while ( WaitForSingleObject( done, 0 ) != WAIT_OBJECT_0 )
            {
                if ( GetTickCount() - start > kWaitMs ) return false;
                XTaskQueueDispatch( queue, XTaskQueuePort::Work, 0 );
                XTaskQueueDispatch( queue, XTaskQueuePort::Completion, 0 );
                Sleep( 1 );
            }
            return true;
        }
    };
}

/* Goes through the real DLL: exported prototype -> QueryApiImpl -> IXNetworkingImpl. */
class XNetworkingIntegration : public XGameRuntimeTests
{
protected:
    void SetUp() override { ASSERT_EQ( XGameRuntimeInitialize(), S_OK ); }
    void TearDown() override { XGameRuntimeUninitialize(); }

    template <typename Start, typename ResultSize, typename Result>
    void RunSecurityQuery( Start start, ResultSize result_size, Result result )
    {
        Query q;
        ASSERT_EQ( start( &q.block ), S_OK );
        ASSERT_TRUE( q.Drain() );

        SIZE_T size = 0;
        ASSERT_EQ( result_size( &q.block, &size ), S_OK );
        EXPECT_EQ( size, sizeof( XNetworkingSecurityInformation ) );

        std::vector<UINT8> buffer( size );
        SIZE_T used = 0;
        XNetworkingSecurityInformation *info = nullptr;
        ASSERT_EQ( result( &q.block, size, &used, buffer.data(), &info ), S_OK );
        EXPECT_EQ( used, size );
        ASSERT_EQ( static_cast<void *>( info ), static_cast<void *>( buffer.data() ) );
        EXPECT_EQ( info->enabledHttpSecurityProtocolFlags, 0x800u );
        EXPECT_EQ( info->thumbprintCount, 0u );
        EXPECT_EQ( info->thumbprints, nullptr );
    }
};

TEST_F( XNetworkingIntegration, PortQueryIsNotImplemented )
{
    UINT16 port = 0;
    EXPECT_EQ( XNetworkingQueryPreferredLocalUdpMultiplayerPort( &port ), E_NOTIMPL );
}

TEST_F( XNetworkingIntegration, PortQueryAsyncIsNotImplemented )
{
    XAsyncBlock block{};
    EXPECT_EQ( XNetworkingQueryPreferredLocalUdpMultiplayerPortAsync( &block ), E_NOTIMPL );
}

TEST_F( XNetworkingIntegration, PortQueryAsyncResultIsNotImplemented )
{
    XAsyncBlock block{};
    UINT16 port = 0;
    EXPECT_EQ( XNetworkingQueryPreferredLocalUdpMultiplayerPortAsyncResult( &block, &port ), E_NOTIMPL );
}

TEST_F( XNetworkingIntegration, PortRegisterIsNotImplemented )
{
    XTaskQueueRegistrationToken token{};
    EXPECT_EQ( XNetworkingRegisterPreferredLocalUdpMultiplayerPortChanged( nullptr, nullptr, PortChanged, &token ), E_NOTIMPL );
}

TEST_F( XNetworkingIntegration, PortUnregisterReturnsFalse )
{
    XTaskQueueRegistrationToken token{ 1 };
    EXPECT_EQ( XNetworkingUnregisterPreferredLocalUdpMultiplayerPortChanged( token, FALSE ), FALSE );
}

TEST_F( XNetworkingIntegration, SecurityInformationForUrlRoundTrips )
{
    RunSecurityQuery(
        []( XAsyncBlock *b ) { return XNetworkingQuerySecurityInformationForUrlAsync( "https://example.com/", b ); },
        XNetworkingQuerySecurityInformationForUrlAsyncResultSize,
        XNetworkingQuerySecurityInformationForUrlAsyncResult );
}

TEST_F( XNetworkingIntegration, SecurityInformationForUrlUtf16RoundTrips )
{
    RunSecurityQuery(
        []( XAsyncBlock *b ) { return XNetworkingQuerySecurityInformationForUrlUtf16Async( L"https://example.com/", b ); },
        XNetworkingQuerySecurityInformationForUrlUtf16AsyncResultSize,
        XNetworkingQuerySecurityInformationForUrlUtf16AsyncResult );
}

TEST_F( XNetworkingIntegration, SecurityInformationRejectsNullArguments )
{
    XAsyncBlock block{};
    EXPECT_EQ( XNetworkingQuerySecurityInformationForUrlAsync( nullptr, &block ), E_INVALIDARG );
    EXPECT_EQ( XNetworkingQuerySecurityInformationForUrlAsync( "https://example.com/", nullptr ), E_INVALIDARG );
    EXPECT_EQ( XNetworkingQuerySecurityInformationForUrlUtf16Async( nullptr, &block ), E_INVALIDARG );
    EXPECT_EQ( XNetworkingQuerySecurityInformationForUrlUtf16Async( L"https://example.com/", nullptr ), E_INVALIDARG );
}

TEST_F( XNetworkingIntegration, VerifyServerCertificateChecksItsArguments )
{
    XNetworkingSecurityInformation info{ 0x800, 0, nullptr };
    int handle = 0;
    EXPECT_EQ( XNetworkingVerifyServerCertificate( &handle, &info ), S_OK );
    EXPECT_EQ( XNetworkingVerifyServerCertificate( nullptr, &info ), E_INVALIDARG );
    EXPECT_EQ( XNetworkingVerifyServerCertificate( &handle, nullptr ), E_INVALIDARG );
    info.thumbprintCount = 1;
    EXPECT_EQ( XNetworkingVerifyServerCertificate( &handle, &info ), E_NOTIMPL );
}

TEST_F( XNetworkingIntegration, ConnectivityHintIsFixed )
{
    XNetworkingConnectivityHint hint{};
    ASSERT_EQ( XNetworkingGetConnectivityHint( &hint ), S_OK );
    EXPECT_EQ( hint.connectivityLevel, XNetworkingConnectivityLevelHint::InternetAccess );
    EXPECT_EQ( hint.connectivityCost, XNetworkingConnectivityCostHint::Unrestricted );
    EXPECT_TRUE( hint.networkInitialized );
    EXPECT_EQ( XNetworkingGetConnectivityHint( nullptr ), E_INVALIDARG );
}

TEST_F( XNetworkingIntegration, ConnectivityHintRegistrationRoundTrips )
{
    XTaskQueueRegistrationToken token{};
    ASSERT_EQ( XNetworkingRegisterConnectivityHintChanged( nullptr, nullptr, HintChanged, &token ), S_OK );
    EXPECT_NE( token.token, 0u );
    EXPECT_EQ( XNetworkingUnregisterConnectivityHintChanged( token, FALSE ), TRUE );
    EXPECT_EQ( XNetworkingUnregisterConnectivityHintChanged( token, FALSE ), FALSE );
}

TEST_F( XNetworkingIntegration, ConfigurationSettingQueryIsNotImplemented )
{
    UINT64 value = 0;
    EXPECT_EQ( XNetworkingQueryConfigurationSetting( XNetworkingConfigurationSetting::MaxTitleTcpQueuedReceiveBufferSize, &value ), E_NOTIMPL );
}

TEST_F( XNetworkingIntegration, ConfigurationSettingSetIsNotImplemented )
{
    EXPECT_EQ( XNetworkingSetConfigurationSetting( XNetworkingConfigurationSetting::MaxTitleTcpQueuedReceiveBufferSize, 1 ), E_NOTIMPL );
}

TEST_F( XNetworkingIntegration, StatisticsQueryIsNotImplemented )
{
    XNetworkingStatisticsBuffer buffer{};
    EXPECT_EQ( XNetworkingQueryStatistics( XNetworkingStatisticsType::TitleTcpQueuedReceivedBufferUsage, &buffer ), E_NOTIMPL );
}
