package com.kite.zchat.main;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.text.InputType;
import android.util.TypedValue;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.TextView;
import android.widget.Toast;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;

import com.google.android.material.button.MaterialButton;
import com.google.android.material.card.MaterialCardView;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;

import com.kite.zchat.MainActivity;
import com.kite.zchat.R;
import com.kite.zchat.auth.AuthActions;
import com.kite.zchat.auth.AuthCredentialStore;
import com.kite.zchat.core.ServerEndpoint;
import com.kite.zchat.crypto.Sha256Utf8;
import com.kite.zchat.profile.LocalProfileStore;
import com.kite.zchat.profile.ProfileDisplayHelper;
import com.kite.zchat.profile.ProfileEditActivity;
import com.kite.zchat.zsp.ZspProfileCodec;
import com.kite.zchat.zsp.ZspSessionManager;

import java.io.IOException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class ProfileFragment extends Fragment {

    private static final String ARG_HOST = "host";
    private static final String ARG_PORT = "port";

    private final ExecutorService io = Executors.newSingleThreadExecutor();

    private final ActivityResultLauncher<Intent> editProfileLauncher =
            registerForActivityResult(
                    new ActivityResultContracts.StartActivityForResult(),
                    result -> {
                        if (result.getResultCode() == Activity.RESULT_OK) {
                            refreshProfileFromServer();
                        }
                    });

    private MaterialCardView profileCard;
    private ImageView profileAvatar;
    private TextView profileDisplayName;
    private TextView profileUserId;

    public static ProfileFragment newInstance(@Nullable String host, int port) {
        ProfileFragment f = new ProfileFragment();
        Bundle b = new Bundle();
        if (host != null) {
            b.putString(ARG_HOST, host);
        }
        b.putInt(ARG_PORT, port);
        f.setArguments(b);
        return f;
    }

    @Nullable
    @Override
    public View onCreateView(
            @NonNull LayoutInflater inflater,
            @Nullable ViewGroup container,
            @Nullable Bundle savedInstanceState) {
        return inflater.inflate(R.layout.fragment_profile_tab, container, false);
    }

    @Override
    public void onViewCreated(@NonNull View view, @Nullable Bundle savedInstanceState) {
        super.onViewCreated(view, savedInstanceState);
        TextView title = view.findViewById(R.id.tabTitle);
        TextView subtitle = view.findViewById(R.id.tabSubtitle);
        TextView serverLine = view.findViewById(R.id.textServerLine);
        title.setText(R.string.tab_profile_title);
        subtitle.setText(R.string.tab_profile_subtitle);

        profileCard = view.findViewById(R.id.profileCard);
        profileAvatar = view.findViewById(R.id.profileAvatar);
        profileDisplayName = view.findViewById(R.id.profileDisplayName);
        profileUserId = view.findViewById(R.id.profileUserId);

        Bundle args = getArguments();
        String host = args != null ? args.getString(ARG_HOST) : null;
        int port = args != null ? args.getInt(ARG_PORT, 0) : 0;
        if (host != null && port > 0) {
            serverLine.setText(getString(R.string.tab_profile_server_line, host, port));
        } else {
            serverLine.setText(R.string.tab_profile_server_unknown);
        }

        bindUserId();

        profileCard.setOnClickListener(
                v ->
                        editProfileLauncher.launch(
                                new Intent(requireActivity(), ProfileEditActivity.class)));

        MaterialButton changeServer = view.findViewById(R.id.buttonChangeServer);
        changeServer.setOnClickListener(v -> showChangeServerConfirmDialog());

        MaterialButton signOut = view.findViewById(R.id.buttonSignOut);
        MaterialButton deleteAccount = view.findViewById(R.id.buttonDeleteAccount);
        signOut.setOnClickListener(v -> showSignOutDialog());
        deleteAccount.setOnClickListener(v -> showDeleteAccountWarnDialog());
    }

    private void showChangeServerConfirmDialog() {
        new MaterialAlertDialogBuilder(requireContext())
                .setTitle(R.string.profile_change_server_confirm_title)
                .setMessage(R.string.profile_change_server_confirm_message)
                .setPositiveButton(
                        R.string.profile_action_confirm_continue,
                        (d, w) -> MainActivity.startEditServer(requireContext()))
                .setNegativeButton(android.R.string.cancel, null)
                .show();
    }

    private void showSignOutDialog() {
        new MaterialAlertDialogBuilder(requireContext())
                .setTitle(R.string.profile_sign_out_title)
                .setMessage(R.string.profile_sign_out_message)
                .setPositiveButton(
                        R.string.profile_sign_out_confirm,
                        (d, w) -> AuthActions.signOut(requireContext()))
                .setNegativeButton(android.R.string.cancel, null)
                .show();
    }

    /** 不可逆操作：先警示，再进入密码确认。 */
    private void showDeleteAccountWarnDialog() {
        new MaterialAlertDialogBuilder(requireContext())
                .setTitle(R.string.profile_delete_warn_title)
                .setMessage(R.string.profile_delete_warn_message)
                .setPositiveButton(
                        R.string.profile_delete_warn_continue,
                        (d, w) -> showDeleteAccountPasswordDialog())
                .setNegativeButton(android.R.string.cancel, null)
                .show();
    }

    private void showDeleteAccountPasswordDialog() {
        String hex = AuthCredentialStore.create(requireContext()).getUserIdHex();
        if (hex.length() != 32) {
            return;
        }
        Context ctx = requireContext();
        final EditText input = new EditText(ctx);
        input.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
        input.setHint(R.string.password_hint);
        int pad =
                (int)
                        TypedValue.applyDimension(
                                TypedValue.COMPLEX_UNIT_DIP, 16f, getResources().getDisplayMetrics());
        FrameLayout wrap = new FrameLayout(ctx);
        FrameLayout.LayoutParams lp =
                new FrameLayout.LayoutParams(
                        FrameLayout.LayoutParams.MATCH_PARENT,
                        FrameLayout.LayoutParams.WRAP_CONTENT);
        lp.setMargins(pad, pad, pad, pad);
        wrap.addView(input, lp);

        new MaterialAlertDialogBuilder(ctx)
                .setTitle(R.string.profile_delete_account_password_title)
                .setMessage(R.string.profile_delete_account_message)
                .setView(wrap)
                .setPositiveButton(
                        R.string.profile_delete_account_confirm,
                        (d, w) -> {
                            String pw = input.getText() != null ? input.getText().toString() : "";
                            if (pw.length() < 8) {
                                Toast.makeText(ctx, R.string.error_password_min, Toast.LENGTH_SHORT).show();
                                return;
                            }
                            if (pw.length() > 512) {
                                Toast.makeText(ctx, R.string.error_password_range, Toast.LENGTH_SHORT).show();
                                return;
                            }
                            runDeleteAccount(hex, pw);
                        })
                .setNegativeButton(android.R.string.cancel, null)
                .show();
    }

    private void runDeleteAccount(String userIdHex, String password) {
        Bundle args = getArguments();
        String host = args != null ? args.getString(ARG_HOST) : null;
        int port = args != null ? args.getInt(ARG_PORT, 0) : 0;
        if (host == null || port <= 0) {
            Toast.makeText(requireContext(), R.string.profile_sync_no_server, Toast.LENGTH_SHORT).show();
            return;
        }
        io.execute(
                () -> {
                    AuthCredentialStore creds = AuthCredentialStore.create(requireContext());
                    if (!creds.hasCredentials() || !isAdded()) {
                        return;
                    }
                    ServerEndpoint ep = new ServerEndpoint(host, port);
                    byte[] uid = creds.getUserIdBytes();
                    if (uid.length != 16) {
                        return;
                    }
                    if (!ZspSessionManager.get().ensureSession(ep, uid, password)) {
                        requireActivity()
                                .runOnUiThread(
                                        () ->
                                                Toast.makeText(
                                                                requireContext(),
                                                                R.string.profile_delete_failed,
                                                                Toast.LENGTH_SHORT)
                                                        .show());
                        return;
                    }
                    byte[] sha = Sha256Utf8.digestPassword(password);
                    boolean ok = ZspSessionManager.get().accountDelete(sha);
                    if (!isAdded()) {
                        return;
                    }
                    requireActivity()
                            .runOnUiThread(
                                    () -> {
                                        if (ok) {
                                            AuthActions.finishAccountDeletion(requireContext(), userIdHex);
                                        } else {
                                            Toast.makeText(
                                                            requireContext(),
                                                            R.string.profile_delete_failed,
                                                            Toast.LENGTH_SHORT)
                                                    .show();
                                        }
                                    });
                });
    }

    @Override
    public void onResume() {
        super.onResume();
        refreshProfileFromServer();
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        io.shutdownNow();
    }

    private void bindUserId() {
        String hex = AuthCredentialStore.create(requireContext()).getUserIdHex();
        if (hex.length() == 32) {
            profileUserId.setText(hex);
            profileDisplayName.setText(ProfileDisplayHelper.effectiveDisplayName(null, hex));
        } else {
            profileUserId.setText(R.string.profile_user_id_unknown);
            profileDisplayName.setText(R.string.profile_nickname_placeholder);
        }
    }

    private void refreshProfileFromServer() {
        Bundle args = getArguments();
        String host = args != null ? args.getString(ARG_HOST) : null;
        int port = args != null ? args.getInt(ARG_PORT, 0) : 0;
        if (host == null || port <= 0) {
            refreshOfflinePresentation();
            return;
        }
        io.execute(
                () -> {
                    AuthCredentialStore creds = AuthCredentialStore.create(requireContext());
                    if (!creds.hasCredentials()) {
                        return;
                    }
                    if (!isAdded()) {
                        return;
                    }
                    ServerEndpoint ep = new ServerEndpoint(host, port);
                    byte[] uid = creds.getUserIdBytes();
                    String pw = creds.getPassword();
                    if (uid.length != 16 || pw.isEmpty()) {
                        return;
                    }
                    if (!ZspSessionManager.get().ensureSession(ep, uid, pw)) {
                        requireActivity().runOnUiThread(this::refreshOfflinePresentation);
                        return;
                    }
                    ZspProfileCodec.UserProfile p = ZspSessionManager.get().userProfileGet(uid);
                    if (!isAdded()) {
                        return;
                    }
                    String hex = creds.getUserIdHex();
                    /*
                     * 资料包为 nick‖avatar；若应答中未带头像（或昵称过长导致包内头像被截断为空），
                     * 与好友详情页一致再拉 USER_AVATAR_GET，避免重装后仅依赖本机已删缓存时出现「无头像」。
                     */
                    byte[] avatarBytes =
                            p.avatarBytes != null && p.avatarBytes.length > 0 ? p.avatarBytes : null;
                    if (avatarBytes == null || avatarBytes.length == 0) {
                        avatarBytes = ZspSessionManager.get().userAvatarGet(uid);
                    }
                    final byte[] avatarForUi = avatarBytes;
                    requireActivity()
                            .runOnUiThread(
                                    () -> {
                                        profileDisplayName.setText(
                                                ProfileDisplayHelper.effectiveDisplayName(p.nicknameUtf8, hex));
                                        if (avatarForUi != null && avatarForUi.length > 0) {
                                            try {
                                                LocalProfileStore.writeAvatarBytes(
                                                        requireContext(), hex, avatarForUi);
                                            } catch (IOException ignored) {
                                            }
                                        }
                                        refreshAvatarImageOnly();
                                    });
                });
    }

    /** 仅刷新本机头像文件；昵称由调用方已根据服务端资料设置。 */
    private void refreshAvatarImageOnly() {
        String hex = AuthCredentialStore.create(requireContext()).getUserIdHex();
        if (hex.length() == 32) {
            LocalProfileStore.loadAvatarInto(profileAvatar, hex, R.drawable.ic_avatar_default);
        } else {
            profileAvatar.setImageResource(R.drawable.ic_avatar_default);
        }
    }

    /** 无法拉取服务端资料时：昵称回退为账户 ID，头像用本机缓存。 */
    private void refreshOfflinePresentation() {
        String hex = AuthCredentialStore.create(requireContext()).getUserIdHex();
        if (hex.length() == 32) {
            profileDisplayName.setText(ProfileDisplayHelper.effectiveDisplayName(null, hex));
            LocalProfileStore.loadAvatarInto(profileAvatar, hex, R.drawable.ic_avatar_default);
        } else {
            profileDisplayName.setText(R.string.profile_nickname_placeholder);
            profileAvatar.setImageResource(R.drawable.ic_avatar_default);
        }
    }
}
