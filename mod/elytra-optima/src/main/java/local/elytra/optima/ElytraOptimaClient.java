package local.elytra.optima;

import com.mojang.blaze3d.platform.InputConstants;
import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.fabricmc.fabric.api.client.keymapping.v1.KeyMappingHelper;
import net.minecraft.client.KeyMapping;
import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.network.chat.Component;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

public final class ElytraOptimaClient implements ClientModInitializer {
    private static final Strategy[] STRATEGIES = new Strategy[] {
        Strategy.segmentedPrefix(
            "gain2_min_drop",
            "最小高度起步（>35m）",
            "Min-start height (>35 m)",
            217,
            new Segments(0, 0, 128, 14, 5, 0, 271, 40),
            -88.61935982554968,
            new double[] {
                -0.01,
                -0.01,
                -0.01,
                -23.154733257869417,
                -15.716026296569407,
                -63.858448303099955,
                -9.930551572139025,
                -49.10599335266985
            },
            new double[] {
                60.00195285516284,
                22.23981811310981,
                8.794913186308051,
                5.559029164842681,
                0.0651262291329768,
                17.541921762239856,
                60.36573726338823,
                0.0
            },
            0.0
        ),
        Strategy.segmented(
            "max_climb",
            "最大提升速度（20m/cycle，起步高度>75m）",
            "Fastest climb (20 m/cycle, start >75 m)",
            254,
            new Segments(2, 5, 141, 15, 4, 0, 83, 4),
            -83.44910138799413,
            new double[] {
                -21.507408848406726,
                -31.064412051040822,
                -17.75203519264177,
                -63.83111872689089,
                -5.024003861649917,
                -83.57492395202101,
                -19.020736082912922,
                -62.2599218701962
            },
            new double[] {
                80.88135214090949,
                59.766059051049844,
                26.760085044938684,
                33.961915704001306,
                7.911228156821989,
                22.564579953207456,
                10.025017993419707,
                0.0
            }
        ),
        Strategy.segmented(
            "hard_speed",
            "最快水平速度（33m/s，起步高度>142m）",
            "Fastest horizontal (33 m/s, start >142 m)",
            357,
            new Segments(10, 2, 246, 13, 3, 5, 70, 8),
            -89.5,
            new double[] {
                -32.84953798913042,
                -40.13245184698956,
                -22.468576627022838,
                -44.7325714278412,
                -49.26187641318844,
                -22.776456702908604,
                -66.27087681959183,
                -38.24817494492914
            },
            new double[] {
                78.32703324587365,
                45.31013945331428,
                42.84497193184144,
                16.96999777207822,
                32.377854556287126,
                1.4634826880848515,
                14.955641228893784,
                0.0
            }
        )
    };

    private static KeyMapping toggleKey;
    private static KeyMapping cycleStrategyKey;
    private static boolean enabled = false;
    private static int strategyIndex = 0;
    private static int tickInCycle = 0;

    @Override
    public void onInitializeClient() {
        toggleKey = KeyMappingHelper.registerKeyMapping(new KeyMapping(
            "key.elytra_optima.toggle",
            InputConstants.Type.KEYSYM,
            72,
            KeyMapping.Category.MISC
        ));
        cycleStrategyKey = KeyMappingHelper.registerKeyMapping(new KeyMapping(
            "key.elytra_optima.cycle_strategy",
            InputConstants.Type.KEYSYM,
            74,
            KeyMapping.Category.MISC
        ));

        ClientTickEvents.END_CLIENT_TICK.register(ElytraOptimaClient::onClientTick);
    }

    private static void onClientTick(Minecraft client) {
        while (toggleKey.consumeClick()) {
            enabled = !enabled;
            tickInCycle = 0;
            if (enabled) {
                strategyIndex = 0;
            }
            boolean chinese = isChinese(client);
            showStatus(
                client,
                enabled
                    ? text(chinese, "鞘翅最优策略: 开 · 策略: ", "Elytra Optima: on · Strategy: ") + currentStrategy().label(chinese)
                    : text(chinese, "鞘翅最优策略: 关", "Elytra Optima: off")
            );
        }

        while (cycleStrategyKey.consumeClick()) {
            strategyIndex = (strategyIndex + 1) % STRATEGIES.length;
            tickInCycle = 0;
            boolean chinese = isChinese(client);
            showStatus(client, text(chinese, "策略: ", "Strategy: ") + currentStrategy().label(chinese));
        }

        if (client.isPaused()) {
            return;
        }

        LocalPlayer player = client.player;
        if (!enabled || player == null) {
            return;
        }

        if (!player.isFallFlying()) {
            tickInCycle = 0;
            return;
        }

        Strategy strategy = currentStrategy();
        double angle = strategy.angleAt(tickInCycle);
        player.setXRot((float) clamp(-angle, -90.0, 90.0));
        tickInCycle = strategy.nextTick(tickInCycle);
    }

    private static Strategy currentStrategy() {
        return STRATEGIES[strategyIndex];
    }

    private static void showStatus(Minecraft client, String message) {
        if (client.player != null) {
            client.player.sendOverlayMessage(Component.literal(message));
        }
    }

    private static String text(boolean chinese, String zh, String en) {
        return chinese ? zh : en;
    }

    private static boolean isChinese(Minecraft client) {
        String selected = client.getLanguageManager().getSelected();
        return selected != null && selected.toLowerCase(Locale.ROOT).startsWith("zh");
    }

    private static double clamp(double value, double min, double max) {
        return Math.max(min, Math.min(max, value));
    }

    private record Segments(
        int negativeConstantTicks,
        int negativeTransitionLinearTicks,
        int negativeBezierTicks,
        int holdZeroAfterNegativeTicks,
        int positiveRampLinearTicks,
        int positiveHoldTicks,
        int positiveBezierToZeroTicks,
        int holdZeroEndTicks
    ) {
        int totalTicks() {
            return negativeConstantTicks
                + negativeTransitionLinearTicks
                + negativeBezierTicks
                + holdZeroAfterNegativeTicks
                + positiveRampLinearTicks
                + positiveHoldTicks
                + positiveBezierToZeroTicks
                + holdZeroEndTicks;
        }
    }

    private record Strategy(String id, String zhLabel, String enLabel, int periodTicks, double[] angles, boolean repeating, double holdAfterAngle) {
        static Strategy segmented(
            String id,
            String zhLabel,
            String enLabel,
            int periodTicks,
            Segments segments,
            double negativeConstantAngle,
            double[] negativeControls,
            double[] positiveControls
        ) {
            if (segments.totalTicks() != periodTicks) {
                throw new IllegalArgumentException(id + " segment ticks do not sum to " + periodTicks);
            }

            List<Double> out = new ArrayList<>(periodTicks);
            appendHold(out, segments.negativeConstantTicks(), negativeConstantAngle);
            appendLinear(out, segments.negativeTransitionLinearTicks(), negativeConstantAngle, negativeControls[0]);
            appendParametricBezier(out, segments.negativeBezierTicks(), negativeControls);
            appendHold(out, segments.holdZeroAfterNegativeTicks(), 0.0);
            appendLinear(out, segments.positiveRampLinearTicks(), 0.0, positiveControls[0]);
            appendHold(out, segments.positiveHoldTicks(), positiveControls[0]);
            appendParametricBezier(out, segments.positiveBezierToZeroTicks(), positiveControls);
            appendHold(out, segments.holdZeroEndTicks(), 0.0);

            double[] angles = new double[out.size()];
            for (int i = 0; i < out.size(); i++) {
                angles[i] = out.get(i);
            }
            return new Strategy(id, zhLabel, enLabel, periodTicks, angles, true, 0.0);
        }

        static Strategy segmentedPrefix(
            String id,
            String zhLabel,
            String enLabel,
            int activeTicks,
            Segments segments,
            double negativeConstantAngle,
            double[] negativeControls,
            double[] positiveControls,
            double holdAfterAngle
        ) {
            int expandedTicks = segments.totalTicks();
            if (activeTicks <= 0 || activeTicks > expandedTicks) {
                throw new IllegalArgumentException(id + " active tick count must be in 1.." + expandedTicks);
            }

            List<Double> out = new ArrayList<>(expandedTicks);
            appendHold(out, segments.negativeConstantTicks(), negativeConstantAngle);
            appendLinear(out, segments.negativeTransitionLinearTicks(), negativeConstantAngle, negativeControls[0]);
            appendParametricBezier(out, segments.negativeBezierTicks(), negativeControls);
            appendHold(out, segments.holdZeroAfterNegativeTicks(), 0.0);
            appendLinear(out, segments.positiveRampLinearTicks(), 0.0, positiveControls[0]);
            appendHold(out, segments.positiveHoldTicks(), positiveControls[0]);
            appendParametricBezier(out, segments.positiveBezierToZeroTicks(), positiveControls);
            appendHold(out, segments.holdZeroEndTicks(), 0.0);

            double[] angles = new double[activeTicks];
            for (int i = 0; i < activeTicks; i++) {
                angles[i] = out.get(i);
            }
            return new Strategy(id, zhLabel, enLabel, activeTicks, angles, true, holdAfterAngle);
        }

        String label(boolean chinese) {
            return chinese ? zhLabel : enLabel;
        }

        double angleAt(int tick) {
            if (!repeating && tick >= angles.length) {
                return holdAfterAngle;
            }
            return angles[Math.floorMod(tick, angles.length)];
        }

        int nextTick(int tick) {
            if (repeating) {
                return (tick + 1) % periodTicks;
            }
            return tick >= angles.length ? tick : tick + 1;
        }

        private static void appendHold(List<Double> out, int ticks, double angle) {
            for (int i = 0; i < ticks; i++) {
                out.add(angle);
            }
        }

        private static void appendLinear(List<Double> out, int ticks, double fromAngle, double toAngle) {
            for (int i = 0; i < ticks; i++) {
                double u = (i + 1.0) / ticks;
                out.add(fromAngle * (1.0 - u) + toAngle * u);
            }
        }

        private static void appendParametricBezier(List<Double> out, int ticks, double[] yControls) {
            if (ticks <= 0) {
                return;
            }
            if (ticks == 1) {
                out.add(yControls[0]);
                return;
            }

            double[] xControls = makeCosineBezierX(yControls.length);
            for (int i = 0; i < ticks; i++) {
                double targetX = i / (ticks - 1.0);
                double u = solveBezierUForX(xControls, targetX);
                out.add(bezierValue(yControls, u));
            }
        }

        private static double[] makeCosineBezierX(int count) {
            double[] controls = new double[count];
            for (int i = 0; i < count; i++) {
                controls[i] = 0.5 - 0.5 * Math.cos(Math.PI * i / (count - 1.0));
            }
            return controls;
        }

        private static double solveBezierUForX(double[] xControls, double targetX) {
            double lo = 0.0;
            double hi = 1.0;
            for (int i = 0; i < 60; i++) {
                double mid = (lo + hi) * 0.5;
                if (bezierValue(xControls, mid) < targetX) {
                    lo = mid;
                } else {
                    hi = mid;
                }
            }
            return (lo + hi) * 0.5;
        }

        private static double bezierValue(double[] points, double u) {
            double[] work = points.clone();
            for (int level = work.length - 1; level > 0; level--) {
                for (int i = 0; i < level; i++) {
                    work[i] = work[i] * (1.0 - u) + work[i + 1] * u;
                }
            }
            return work[0];
        }
    }
}
