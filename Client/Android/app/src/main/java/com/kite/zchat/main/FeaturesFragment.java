package com.kite.zchat.main;

import android.content.Intent;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;

import com.google.android.material.card.MaterialCardView;
import com.google.android.material.color.MaterialColors;

import com.kite.zchat.AddFriendActivity;
import com.kite.zchat.FriendRequestsActivity;
import com.kite.zchat.MainPlaceholderActivity;
import com.kite.zchat.R;
import com.kite.zchat.friends.FriendPendingRequests;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class FeaturesFragment extends Fragment {

    private static final String ARG_HOST = "host";
    private static final String ARG_PORT = "port";

    private final ExecutorService io = Executors.newSingleThreadExecutor();

    private MaterialCardView cardFriendRequests;
    private TextView friendRequestsBadgeCount;

    public static FeaturesFragment newInstance(@Nullable String host, int port) {
        FeaturesFragment f = new FeaturesFragment();
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
        return inflater.inflate(R.layout.fragment_features, container, false);
    }

    @Override
    public void onViewCreated(@NonNull View view, @Nullable Bundle savedInstanceState) {
        super.onViewCreated(view, savedInstanceState);
        cardFriendRequests = view.findViewById(R.id.cardFriendRequests);
        friendRequestsBadgeCount = view.findViewById(R.id.friendRequestsBadgeCount);

        Bundle args = getArguments();
        String host = args != null ? args.getString(ARG_HOST) : null;
        int port = args != null ? args.getInt(ARG_PORT, 0) : 0;

        view.findViewById(R.id.rowAddFriend)
                .setOnClickListener(
                        v -> {
                            Intent i = new Intent(requireContext(), AddFriendActivity.class);
                            i.putExtra(AddFriendActivity.EXTRA_HOST, host);
                            i.putExtra(AddFriendActivity.EXTRA_PORT, port);
                            startActivity(i);
                        });
        view.findViewById(R.id.rowFriendRequests)
                .setOnClickListener(
                        v -> {
                            Intent i = new Intent(requireContext(), FriendRequestsActivity.class);
                            i.putExtra(FriendRequestsActivity.EXTRA_HOST, host);
                            i.putExtra(FriendRequestsActivity.EXTRA_PORT, port);
                            startActivity(i);
                        });
    }

    @Override
    public void onResume() {
        super.onResume();
        refreshPendingBadgeUi();
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        io.shutdownNow();
    }

    private void refreshPendingBadgeUi() {
        Bundle args = getArguments();
        String host = args != null ? args.getString(ARG_HOST) : null;
        int port = args != null ? args.getInt(ARG_PORT, 0) : 0;
        if (host == null || host.isBlank() || port <= 0) {
            postBadgeUi(0);
            return;
        }
        io.execute(
                () -> {
                    int n = FriendPendingRequests.fetchPendingCount(requireContext(), host, port);
                    if (getActivity() == null) {
                        return;
                    }
                    requireActivity()
                            .runOnUiThread(
                                    () -> {
                                        if (!isAdded()) {
                                            return;
                                        }
                                        postBadgeUi(n);
                                        if (getActivity() instanceof MainPlaceholderActivity) {
                                            ((MainPlaceholderActivity) getActivity())
                                                    .syncFriendRequestTabBadge(n);
                                        }
                                    });
                });
    }

    private void postBadgeUi(int count) {
        if (friendRequestsBadgeCount == null || cardFriendRequests == null) {
            return;
        }
        int outline =
                MaterialColors.getColor(
                        cardFriendRequests, com.google.android.material.R.attr.colorOutlineVariant, 0);
        int primary =
                MaterialColors.getColor(
                        cardFriendRequests, com.google.android.material.R.attr.colorPrimary, 0);
        int stroke1 = Math.max(1, (int) (getResources().getDisplayMetrics().density + 0.5f));
        int stroke2 = Math.max(2, (int) (2f * getResources().getDisplayMetrics().density + 0.5f));
        if (count <= 0) {
            friendRequestsBadgeCount.setVisibility(View.GONE);
            cardFriendRequests.setStrokeWidth(stroke1);
            cardFriendRequests.setStrokeColor(outline);
        } else {
            friendRequestsBadgeCount.setVisibility(View.VISIBLE);
            friendRequestsBadgeCount.setText(count > 99 ? "99+" : String.valueOf(count));
            cardFriendRequests.setStrokeWidth(stroke2);
            cardFriendRequests.setStrokeColor(primary);
        }
    }
}
