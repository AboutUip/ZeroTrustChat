package com.ztrust.zchat.im.zsp.server;

import com.ztrust.zchat.im.zsp.ZspFrameDecoder;
import io.netty.bootstrap.ServerBootstrap;
import io.netty.channel.Channel;
import io.netty.channel.ChannelFuture;
import io.netty.channel.ChannelInitializer;
import io.netty.channel.ChannelOption;
import io.netty.channel.EventLoopGroup;
import io.netty.channel.nio.NioEventLoopGroup;
import io.netty.channel.socket.SocketChannel;
import io.netty.channel.socket.nio.NioServerSocketChannel;
import io.netty.handler.timeout.IdleStateHandler;
import org.springframework.context.SmartLifecycle;
import org.springframework.stereotype.Component;

import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.NetworkInterface;
import java.net.SocketAddress;
import java.net.SocketException;
import java.util.ArrayList;
import java.util.Enumeration;
import java.util.List;
import java.util.concurrent.TimeUnit;
import java.util.logging.Level;
import java.util.logging.Logger;

@Component
public final class ZspNettyServer implements SmartLifecycle {

    private static final Logger LOG = Logger.getLogger(ZspNettyServer.class.getName());

    private final ZspServerProperties props;
    private final ZspInboundHandler inboundHandler;

    private volatile boolean running;
    private EventLoopGroup boss;
    private EventLoopGroup worker;
    private Channel serverChannel;

    public ZspNettyServer(ZspServerProperties props, ZspInboundHandler inboundHandler) {
        this.props = props;
        this.inboundHandler = inboundHandler;
    }

    @Override
    public void start() {
        if (running) {
            return;
        }
        if (!props.isEnabled()) {
            LOG.info("ZSP server disabled (zchat.zsp.enabled=false)");
            return;
        }
        boss = new NioEventLoopGroup(props.getBossThreads());
        int w = props.getWorkerThreads();
        worker = w > 0 ? new NioEventLoopGroup(w) : new NioEventLoopGroup();

        ServerBootstrap bootstrap = new ServerBootstrap();
        bootstrap.group(boss, worker)
                .channel(NioServerSocketChannel.class)
                .childOption(ChannelOption.TCP_NODELAY, true)
                .childHandler(new ChannelInitializer<SocketChannel>() {
                    @Override
                    protected void initChannel(SocketChannel ch) {
                        ch.pipeline()
                                .addLast(new IdleStateHandler(0, 0, props.getReaderIdleSeconds(), TimeUnit.SECONDS))
                                .addLast(new ZspIdleEventHandler())
                                .addLast(new ZspFrameDecoder())
                                .addLast(inboundHandler);
                    }
                });
        try {
            ChannelFuture future = bootstrap.bind(props.getPort()).sync();
            serverChannel = future.channel();
            running = true;
            SocketAddress local = serverChannel.localAddress();
            if (local instanceof InetSocketAddress inet) {
                LOG.info("ZSP TCP listening on " + normalizeBindIp(inet.getAddress()) + ":" + inet.getPort());
            } else {
                LOG.info("ZSP TCP listening on port " + props.getPort());
            }
            logClientConnectHints();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            shutdownGroups();
            throw new IllegalStateException("ZSP bind interrupted", e);
        } catch (Exception e) {
            shutdownGroups();
            throw e;
        }
    }

    private void shutdownGroups() {
        if (boss != null) {
            boss.shutdownGracefully();
        }
        if (worker != null) {
            worker.shutdownGracefully();
        }
        boss = null;
        worker = null;
    }

    @Override
    public void stop() {
        if (!running) {
            return;
        }
        try {
            if (serverChannel != null) {
                serverChannel.close().sync();
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        } finally {
            shutdownGroups();
            serverChannel = null;
            running = false;
            LOG.info("ZSP server stopped");
        }
    }

    @Override
    public boolean isRunning() {
        return running;
    }

    private void logClientConnectHints() {
        List<String> candidates = listCandidateIpv4();
        if (candidates.isEmpty()) {
            LOG.info("Client connect hint: 127.0.0.1:" + props.getPort());
            return;
        }
        for (String ip : candidates) {
            LOG.info("Client connect hint: " + ip + ":" + props.getPort());
        }
    }

    private static List<String> listCandidateIpv4() {
        List<String> ips = new ArrayList<>();
        try {
            Enumeration<NetworkInterface> interfaces = NetworkInterface.getNetworkInterfaces();
            while (interfaces != null && interfaces.hasMoreElements()) {
                NetworkInterface nif = interfaces.nextElement();
                if (!nif.isUp() || nif.isLoopback() || nif.isVirtual()) {
                    continue;
                }
                Enumeration<InetAddress> addresses = nif.getInetAddresses();
                while (addresses.hasMoreElements()) {
                    InetAddress addr = addresses.nextElement();
                    if (addr instanceof Inet4Address && !addr.isLoopbackAddress()) {
                        ips.add(addr.getHostAddress());
                    }
                }
            }
        } catch (SocketException ex) {
            // Keep startup robust; fallback hint is logged by caller.
        }
        return ips;
    }

    private static String normalizeBindIp(InetAddress address) {
        if (address == null || address.isAnyLocalAddress()) {
            return "0.0.0.0";
        }
        return address.getHostAddress();
    }
}
